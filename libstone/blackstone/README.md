# Blackstone v0

Blackstone specifies guarded compiler rewrites in `.bst` files. This initial
implementation includes a **native dparser frontend**, source-located AST and
validation, a typed reference HIR adapter, inheritance-driven integer folding,
and Python and **native C fixed-point rewrite drivers**. The acceptance kernel is
the actual `kernels/expressions/AlgebraicSimplification.bst`: 16 local rules plus the inherited
built-in `ConstantFolding` entry.

The [extended catalog](kernels/CATALOG.md) adds **84 kernels** across expressions,
statements, memory, control flow, loops and polyhedra. It contains 85 kernels and
100 local rules in total. Expression rules execute on the reference adapter;
structured-host rules require their registered host proofs and descriptor semantics.

The [24-chapter manual](manual/README.md) covers the language, execution model,
Python/native APIs and compiler integration. The [runnable example collection](examples/README.md)
includes 23 HIR fixtures, seven teaching kernels, Python API demos and a C profile
example, with 27 checked cases for both rewrite engines.

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
export BLACKSTONE_LIBRARY="$PWD/build/libblackstone.so"
export PYTHONPATH="$PWD/python"
python3 -m blackstone parse kernels/expressions/AlgebraicSimplification.bst
python3 -m blackstone check kernels/expressions/AlgebraicSimplification.bst
python3 -m blackstone check kernels
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --trace
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --engine native --trace
python3 examples/run_examples.py --engine both
./build/blackstone-native-example
```

`BLACKSTONE_DPARSER_DIR` is a CMake cache override for the supplied dependency.
`BLACKSTONE_PARSER` selects the built native executable; if omitted, the frontend
looks for `build/blackstone-parser` beside this README. `parse` prints the
source-spanned AST as JSON. `check` prints local and inherited rule counts.
`apply` writes optimized HIR JSON to stdout and optional JSON-line traces to
stderr. Diagnostics cause exit status 1. `--budget N` bounds successful rewrites
(default 10000); exhaustion includes recent rule names and, with `--trace`, the
completed trace. Output is published only after optimization succeeds.

`--engine native` lowers the resolved frontend AST into a compiled C profile and
runs matching, guard sequencing, construction validation, scheduling, memoization
and budget enforcement in `libblackstone`. The bridge supplies arbitrary-width
integer operations and host metadata through the same `TreeAdapter` callbacks.
`BLACKSTONE_LIBRARY` selects the shared library; without it the bridge searches
the local `build` directory. Use the platform's `.dylib`/`.dll` filename where
appropriate. Python is the default engine.

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
`hir.TreeAdapter`, `rewrite.Driver.optimize`, and `native.Driver.optimize`.
Native drivers also support `close()` and context-manager lifetime management.
The adapter provides:

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

### Native compiler API

The previously scaffolded `include/`, `concern/`, `requires/`, `profile/`,
`rewrite/`, and loop-descriptor standard-library files implement a standalone C11
runtime. Link the CMake target `Blackstone::Runtime` or `libblackstone` and include
`Blackstone-Rewrite.h`. The six public headers expose registries, typed views,
source-spanned kernel IR, compiled profiles, host callbacks, traces and diagnostics.

`bs_profile_compile` independently resolves declarations/inheritance and validates
postorder match/build instructions. Concern and capability module loading is
atomic. Compiled profiles own their IR, preserve deterministic base/source order,
and can be saved/reloaded with a bounded, versioned, pointer-free binary encoding.
`bs_optimize` publishes a new root only on success; host-owned inputs are immutable.

See [the native API guide](docs/native-api.md) for ownership, callback semantics,
the profile format, and compiler integration. The source grammar remains the
dparser grammar; the profile `parser.h` is the AST-to-IR boundary.

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

`tests/test_native.py` runs the same semantic tests and extended-catalog proof
gates with the C compiler/matcher/driver, plus native CLI, 20,000-bit integer,
callback failure, closed-profile and expanding-kernel cases. The separate
`blackstone-native` CTest exercises the public C ABI directly, module rollback,
unchecked-IR diagnostics, inheritance diamonds/cycles, adapter violations, and
binary profile round-trips, every truncated prefix, and single-byte corruption.

The `blackstone-examples` CTest checks the documented manifest on both engines;
`blackstone-native-example` runs the public-API C demonstration. See
[`examples/README.md`](examples/README.md) for individual fixture commands and
expected results.

To run the native memory/undefined-behavior checks:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --target blackstone-native-test --parallel
./build-sanitize/blackstone-native-test
```
