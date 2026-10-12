# Blackstone v0

Blackstone specifies guarded compiler rewrites in `.bst` files. This initial
implementation includes a **native dparser frontend**, source-located AST and
validation, a typed reference HIR adapter, inheritance-driven integer folding,
and a deterministic fixed-point rewrite driver. The acceptance kernel is the
actual `kernels/AlgebraicSimplification.bst`: 16 local rules plus the inherited
built-in `ConstantFolding` entry.

The [extended catalog](kernels/CATALOG.md) adds **84 kernels** across expressions,
statements, memory, control flow, loops and polyhedra. It contains 85 kernels and
100 local rules in total. Expression rules execute on the reference adapter;
structured-host rules require their registered host proofs and descriptor semantics.

## Build and run

Requirements: C compiler, CMake 3.20+, and Python 3.9+. No Python packages are
required. dparser is compiled directly from `../../third_party/dparser`; its
upstream build expects a differently named source file, so this build explicitly
uses the supplied `write_tables.c`. Grammar tables are generated in the build
directory, not checked into the source tree.

From `libstone/blackstone`:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure

export BLACKSTONE_PARSER="$PWD/build/blackstone-parser"
export PYTHONPATH="$PWD/python"
python3 -m blackstone parse kernels/AlgebraicSimplification.bst
python3 -m blackstone check kernels/AlgebraicSimplification.bst
python3 -m blackstone check kernels
python3 -m blackstone apply kernels/AlgebraicSimplification.bst \
  examples/reassociate.json --trace
```

`BLACKSTONE_DPARSER_DIR` is a CMake cache override for the supplied dependency.
`BLACKSTONE_PARSER` selects the built native executable; if omitted, the frontend
looks for `build/blackstone-parser` beside this README. `parse` prints the
source-spanned AST as JSON. `check` prints local and inherited rule counts.
`apply` writes optimized HIR JSON to stdout and optional JSON-line traces to
stderr. Diagnostics cause exit status 1. `--budget N` bounds successful rewrites
(default 10000); exhaustion includes recent rule names and, with `--trace`, the
completed trace. Output is published only after optimization succeeds.

For additional inheritance dependencies, repeat `--kernel-dir DIR`. All `.bst`
files in those directories (searched recursively) are loaded by their declared kernel name. Dependencies
resolve base-first in listed order, shared ancestors execute once, and duplicate
rule names across the resolved graph are rejected. `ConstantFolding` is supplied
as a built-in entry only when no kernel with that name is supplied.

The example optimizes signed 8-bit `(x plus 3) plus 4` to `x plus 7`:

```text
ReassociateAddConstants: ((x plus 3) plus 4) -> (x plus (3 plus 4))
ConstantFolding:         (3 plus 4) -> 7
```

## HIR fixture format

Fixtures are JSON objects with `root` (node ID) and `nodes` (ID-to-node map).
References may share nodes. Node IDs specify **identity**, not textual or
structural equality. Every node has:

* `kind`: `literal`, `input`, or `binary`.
* `type`: `{ "domain": "integer", "width": 8, "signed": false }`.
  Defaults are scalar unsigned integer, width 32. Width must be positive.
  Other domains can be represented as metadata, but fail integer guards.
* `resolved`: Boolean, default true.
* `provenance`: source/debug label, default the input node ID.
* `origin`: originating rewrite rule, default empty.

Literal nodes have a decimal `value` (prefer a string to avoid JSON consumer
precision loss). Values must be numerically representable in the exact type;
they are not silently reduced modulo the width. Concrete literals are pure and
total. Poison/undef and potentially trapping values are represented as `input`
nodes with appropriate facts, not concrete literals.

Input nodes optionally provide `pure` and `total`: `true`, `false`, or `null`
(unknown, the default). `op` is their descriptive name, default the node ID.
Binary nodes have `op`, exactly two operand IDs in `args`, and optional `flags`
(string list, default empty). Binary nodes can carry explicit `pure`/`total`
proofs; otherwise the adapter derives conservative facts from the operation and
its operands. Explicit true facts are host proofs, not facts inferred from text.
Mixed types and unrecognized/altered semantic flags block integer rewrites.
Cycles, including unreachable cycles, are rejected.

The serializer assigns deterministic output IDs, preserves sharing, and carries
provenance/origin fields. It does not preserve original JSON node-ID spelling.

## Frontend and validation

`grammar/blackstone.g` implements the brief's concrete syntax with dparser's
scannerless lexical matching. `src/parser.c` handles whitespace and non-nesting
comments, rejects unterminated block comments/backticks, and exports the native parse
tree with byte offsets. `python/blackstone/frontend.py` converts that tree into
immutable value-owned AST objects; it does not tokenize or reparse source.
Identifiers are ASCII and keyword productions enforce identifier boundaries.
Backtick operators have equal precedence and associate left-to-right.
Parentheses establish explicit nested trees. Malformed operators/delimiters
produce source-located syntax diagnostics.

`resolve.py` registers the explicit Term/Binary view mapping, operators,
refinements, capabilities, aliases, and arities. It checks concerns/imports,
capture numbering, earlier-only identity references, exact signature/match
trees, replacement sorts, duplicate names, and inheritance cycles. Integer
spellings stay strings in the AST; chunked conversion avoids both machine-word
overflow and Python's decimal-string input limit. An unrepresentable exact
integer fails matching; an unrepresentable replacement fails construction.

### Additional v0 decisions

* The reference adapter is an **occurrence tree**, even when serialization shares
  nodes. Every operand occurrence denotes an evaluation. Sharing establishes
  identity but does not itself establish purity or totality.
* All nonempty semantic-flag lists are conservatively excluded from registered
  modular operations. No flags transfer to newly constructed operators.
* The built-in constant-folder is one ordered driver entry handling all seven
  registered operators. Exceptional division remains intact.
* `-> Binary` requires an explicit binary replacement: a general Term capture
  cannot statically guarantee that result sort.
* Integer binary construction requires the matched skeleton to satisfy the
  reference modular operation contract. Structured-host construction requires
  proven host guards and a homogeneous registered carrier domain. Capture
  forwarding still checks exact result type.
* Guard conjunctions flatten parentheses without changing left-to-right order.
  Only the Boolean value `True` (proven true) allows construction.
* Shared input nodes preserve shared identity during context-independent
  optimization. Trace ordering is deterministic postorder; rewrites of a shared
  node are traced once, then the normalized node is reused at its occurrences.
* Identity/no-change candidates are skipped without spending rewrite budget.

## Adapter and driver boundaries

The public Python entry points are `frontend.parse/load`, `resolve.resolve`,
`hir.TreeAdapter`, and `rewrite.Driver.optimize`. The adapter provides:

1. Resolved typed values and explicit binary operands/opcodes (`Value`, `Type`).
2. Stable identity via `identity`, independent of structural equality.
3. Tri-state property/analysis queries via `predicate` and `fact`.
4. Typed literal and binary construction preserving source/debug provenance.

The reference guards inspect metadata only. They never evaluate input programs.
`ModularInteger` checks **every exposed binary node** and its immediate operands
for homogeneous scalar integer types and unflagged registered semantics; it does
not recursively impose these semantics on opaque Term captures. `Pure` and
`Total` inspect those captured expressions conservatively. Replacements are
built immutably after all guards succeed and validated before being installed.

A compiler integration can implement the same adapter query/construction
contracts or translate its HIR into the value-owned reference form. A native SSA
integration additionally needs its own installation/def-use requeue layer: SSA
references read already-produced values, so their facts differ from occurrence
trees. Effectful defining instructions and effect dependencies must survive
dead-use elimination. The reference driver does not supply an SSA mutation layer.

The supplied fixture's oriented identities, movement, reassociation, and folding
follow the `(B, L, D)` termination argument in `AGENTS.md`. Arbitrary user rules
are controlled by the rewrite budget. Static validation establishes well-formed
rules and contracts; it is not an equivalence prover for arbitrary user rules.

## Verification

The CTest target runs `tests/test_contract.py` against the generated native
parser and the actual `.bst` fixture. It covers all 16 rules through the real
matcher/driver, exhaustive signed/unsigned widths 1–4 against an independent
integer evaluator, all literal pairs for the seven operators, exceptional
division, exact type preservation, wraparound, and signed one-bit behavior.
It also covers source-located negative diagnostics, comments, imports, aliases,
multiline/nested signatures, large integer spellings, inheritance diamonds and
cycles, unsafe semantic domains/flags, missing facts, effect/trap/divergence/
poison/undef cases, identity-distinct expressions, effectful operand evaluation
count/order, short-circuit guards, fixed-point cleanup, deterministic traces,
provenance, idempotence, bounded nonconvergence, and CLI execution.
