# Blackstone native API, ABI v1

The native runtime implements declaration resolution, compiled pattern matching,
guard execution, replacement validation, and deterministic occurrence-tree
rewriting. The source frontend is `grammar/blackstone.g` and `src/parser.c`;
`python/blackstone/native.py` demonstrates lowering its AST to C IR and connecting
the typed reference adapter to the runtime.

## Build and integration

```sh
cmake -S libstone/blackstone -B /tmp/opencode/blackstone-build
cmake --build /tmp/opencode/blackstone-build --parallel
ctest --test-dir /tmp/opencode/blackstone-build --output-on-failure
```

The CMake target is `Blackstone::Runtime`. Public headers are in `include/`:

| Header | Boundary |
| --- | --- |
| `Blackstone-API.h` | ABI version, handles, source locations, statuses, registry lifetime |
| `Blackstone-Concern.h` | View/operator modules, typed value inspection |
| `Blackstone-Requires.h` | Capability modules, aliases, arities, host proof domains |
| `Blackstone-Kernel.h` | Value-owned frontend IR contracts |
| `Blackstone-Profile.h` | Compilation, profile lifetime, binary persistence |
| `Blackstone-Rewrite.h` | Adapter callbacks, optimization options, statistics, traces |

`cmake --install` installs the library, parser executable and public headers.
`tests/native_runtime.c` is a working C-only integration example using those
headers. It supplies a small unsigned-8-bit host and builds IR directly.

The usual host lifecycle is:

1. `bs_registry_create(&error)` installs seven modular integer operators,
   Term/Binary views, the five acceptance capabilities and loop descriptor views.
2. `bs_concern_load` and `bs_requires_load` register additional contracts. The
   descriptors and strings are copied; an unsuccessful module load rolls back the
   whole module. These loaders accept in-memory modules, with explicit names.
3. Lower frontend ASTs to `BSKernel` descriptors and call
   `bs_profile_compile(registry, kernels, count, root_name, &profile, &error)`.
4. Supply a `BSAdapter` with `abi_version = BS_ABI_VERSION` and call
   `bs_optimize(profile, &adapter, root, &options, &new_root, &stats, &error)`.
5. Install `new_root` after `BS_OK`, then destroy the profile and registry.

Status codes distinguish malformed declarations/IR, allocation failure,
nonconvergence, invalid adapter results, and I/O errors. `BSError.message` includes
the available source path, line and column. Output profile/root pointers are
cleared on failure; successful rewrites are available as trace events even when
the run subsequently exhausts its budget.

## Frontend IR and compilation

`BSMatchNode` arrays combine a signature with its match opcode tree. The frontend
must establish that the parsed signature and match have the same tree; lowering
walks both together. The C compiler checks the resulting tree independently:

* Children are zero-based indices that precede their parent in the array.
* The last instruction is the root. Every instruction is reachable exactly once.
* Binary instructions name registered operators and do not bind capture slots.
* Term instructions bind consecutive one-based captures in left-to-right order.
* `BS_SAME` references an earlier capture. `BS_EXACT` stores decimal spelling.

For `Binary(Term!resolves, Term!exact(0))` matching `$1 plus $2`, the instructions
are Term `$1`, exact Term `$2`, then Binary `plus` with children `(0, 1)`.

`BSBuildNode` arrays are also postorder, with capture, integer and binary
instructions. Binary construction uses child indices; repeated capture uses are
separate instructions referring to the same capture. Result `BS_BINARY` requires
an explicit binary root. Guards carry their short names and one-based arguments;
their array order is the source `&&` order.

Kernel descriptors retain each kernel's own concerns/imports and its listed
inheritance order. Resolution visits bases first and each kernel once. The
built-in `ConstantFolding` entry is supplied only if that kernel is absent.
Duplicate kernel names, inherited rule names, cycles, unknown views/operators/
capabilities, ambiguous imports, invalid refinements/captures and malformed
instruction trees are errors. Capability aliases and their declared short names
resolve to the same contract. Unused imports are allowed.

Host operators declare a carrier domain. A structured-host rule must use a
capability with the same nonempty `proof_domain`; this capability certifies the
entire transformation. A rule cannot mix carrier domains. Registration supplies
contracts; a successful runtime proof query supplies the actual proof.

Profiles own copied names, locations, instructions, arguments and resolved
capability descriptors. Frontend IR can be released immediately after compilation.
The registry must outlive profiles. Registry lookup results are borrowed and may
be invalidated by subsequent registration. Registry mutation is not concurrent
with compilation or optimization. Independent runs can share an immutable profile
and registry if their host adapters are thread-safe.

## Typed adapter contract

`BSValue` is a nonzero stable host handle, such as an arena index or node pointer.
`inspect` fills `BSValueView`: kind, resolvedness, domain, width, signedness,
optional canonical `type_key`, opcode, concrete literal spelling, semantic flag
count, and operands. It never executes the source program. View strings and all
handles must remain valid for the whole run.

The exact type comparison includes domain, width, signedness and `type_key`.
Domain `integer` means scalar modular integer. Other domains remain opaque typed
carriers. If the host has further type distinctions, it supplies canonical type
tokens rather than mapping distinct types to the same view.

A concrete literal's decimal spelling is its **representable numeric value**,
not an unchecked bit-pattern spelling. The runtime compares arbitrary-length
decimal strings without machine integer conversion, accepting equivalent leading
zero spellings. A signed one-bit literal is `0` or `-1`, so `exact(1)` cannot match
it. The host validates representability when creating its literal views.

| Callback | Contract |
| --- | --- |
| `inspect` | Return a valid view, or zero for an adapter failure |
| `identity` | Stable value identity and compatible type; structural equality is insufficient |
| `equivalent` | Optional no-change comparison; must not equate identity-distinct opaque inputs |
| `query` | Tri-state capability proof, using the root, arguments, and exposed skeleton |
| `literal` | Construct the requested exact integer at the source root's type, or decline with zero |
| `binary` | Construct the opcode/operands at the source type, unflagged, or decline with zero |
| `rebuild` | Replace normalized operands immutably, preserving operator, type, flags and valid metadata |
| `fold` | Fold a concrete integer pair at its exact type; decline exceptional division |
| `trace` | Observe a successful rewrite's rule/location, old/new handles, and one-based step |

`query` receives the registered alias (for example `Associative`). Only `BS_TRUE`
passes; false and unknown short-circuit. The runtime independently enforces the
built-in `ModularInteger` contract on every exposed binary and its immediate
operands. It excludes mixed types, unresolved values, other domains and nonempty
semantic flags. Captured subtree interiors remain opaque to this guard.

If `query` is omitted, the runtime can prove only registered root associativity/
commutativity and modular eligibility. Purity/totality and structured-host proofs
remain unknown. Omitting construction/folding callbacks declines those candidates.
Returning an invalid view, wrong type/sort, wrong constructed opcode/operands,
altered rebuild metadata, or the wrong requested literal is an adapter error.

All guards finish before construction. Constructors preserve source/debug
provenance and set the originating rule name. Reused captures retain their
identity and provenance. The host owns all constructed values, including unused
no-change candidates and values from failed runs; a per-run arena is convenient.
Callbacks must report failure via their documented return values, without
throwing exceptions across the C ABI. The Python bridge catches callback errors
and propagates them after the C call returns.

## Scheduling and bounds

The driver normalizes operands first, takes the first eligible non-identity rule,
revisits replacement operands and restarts rule order. A per-run handle map
preserves shared identity and avoids repeatedly tracing shared normalization.
Operand **occurrences** still represent evaluations; sharing is not memoized
source-program execution or a purity proof.

Default options are 10,000 successful rewrites and depth 256. A supplied budget
of zero permits no-change candidates only. Depth must be in `1..BS_MAX_DEPTH`;
depth exhaustion reports nonconvergence. Cyclic host expressions are rejected.
Profiles allow at most 4,096 resolved rules, 4,096 instructions per tree, and
instruction/inheritance depth 256. The public width field supports `1..2^32-1`;
integer values themselves have no machine-word size restriction.

The driver publishes the output root only after the whole run succeeds. Traces
are deterministic postorder and include the originating rule's source location.
Budget diagnostics name the pending rule and up to eight recent rules. Statistics
report successful rewrites and the number of distinct visited handles.

An SSA compiler must supply an installation/def-use worklist layer. SSA reads
refer to already-produced values; their purity/totality facts differ from tree
evaluation facts. Effectful definitions and effect dependencies remain live after
rewritten uses. The callback/typed contracts can be shared, but `bs_optimize`
itself uses immutable occurrence-tree semantics.

## Binary profiles

`bs_profile_write`/`bs_profile_read` use `FILE *` streams. Encoding v1 begins with
`BSTP` and four version bytes `01 00 00 00`; numbers are unsigned little-endian
32-bit fields and strings are length-prefixed UTF-8/source bytes without NUL.
It stores rule order, folding markers, locations, match/build instructions,
guards, arguments, and qualified capability names. No pointers or host code are
serialized. The maximum encoding size is 16 MiB. Each input stream contains one
profile; trailing bytes are rejected.

Reading validates the header, counts, string boundaries, all instruction trees,
symbols, capability arities/proof domains and duplicate names against the caller's
registry. A binary profile therefore does not bypass declaration validation.
Host facts, integer arithmetic, allocations and installation remain adapter
responsibilities.

## Actual-file demonstration

From `libstone/blackstone`, using the build above:

```sh
BLACKSTONE_PARSER=/tmp/opencode/blackstone-build/blackstone-parser \
BLACKSTONE_LIBRARY=/tmp/opencode/blackstone-build/libblackstone.so \
PYTHONPATH=python python3 -m blackstone apply \
  kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --engine native --trace
```

The native matcher applies `ReassociateAddConstants`, then inherited
`ConstantFolding`, producing signed-8-bit `x plus 7` with the input `x` identity
and debug provenance preserved.
