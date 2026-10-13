# Cephyr C11 compiler

Cephyr targets ISO C11 and uses the supplied **cparser-iso11** C++ frontend.
Its implemented language is a **C11 scalar/pointer subset**, targeting x86-64
Linux System V with the LP64 data model. `cephyr/libc` supplies the default
headers, static runtime and startup object. Full ISO C11 conformance remains pending.

## Build

```sh
cmake -S . -B build -DMLVM_ENABLE_CEPHYR=ON \
  -DMLVM_CPARSER_SOURCE_DIR=../third_party/cparser-iso11 \
  -DMLVM_UCPP_SOURCE_DIR=../third_party/ucpp
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`MLVM_CPARSER_SOURCE_DIR` and `MLVM_UCPP_SOURCE_DIR` default to
`${MLVM_THIRD_PARTY_DIR}/cparser-iso11` and `${MLVM_THIRD_PARTY_DIR}/ucpp`,
which resolve to DOMWeave's root `third_party` entries in both standalone
and composed builds. `AUTO` enables Cephyr when these sources exist; `ON` fails
with the missing dependency path; `OFF` omits the frontend. Python 3 is used at
configure time to prepare the embedding adapters. There is no download, grammar
generation, Tree-sitter dependency, or subprocess compiler invocation for
code generation.

The tracked cparser adapter in `cephyr/src/parse/cparser/` patches a build-local copy.
It converts parser/lexer termination into structured diagnostics, closes input
streams, reclaims orphan ASTs after errors, bounds numeric literal conversion,
fixes unary signs and comma expressions, preserves declaration ordering and
unnamed parameters, and retains `_Static_assert`/`_Generic` nodes. The downloaded
source tree is not modified. Adapter replacements fail when the expected upstream
source layout changes. The dependency's MIT notice is installed with MLVM.

## Runtime and sysroot selection

```sh
build/cephyr program.c helper.c -o program
./program

# Build Cephyr with a different static runtime (suppresses the bundled build).
cmake -S . -B build-external -DMLVM_ENABLE_CEPHYR=ON \
  -DMLVM_CEPHYR_SYSROOT=/opt/my-libc

# Or select a runtime per invocation.
build/cephyr --sysroot=/opt/my-libc program.c -o program
```

The sysroot layout is `include/`, `lib/crt0.o`, and `lib/libc.a`. Startup must
define `_start` and arrange the call to `main`. The built-in linker consumes
static x86-64 ELF objects and archives, using absolute64/PC32/PLT32 relocations;
it does not perform dynamic linking. External runtimes must satisfy that contract
(host C compilation with `-fno-pie -mcmodel=large` is suitable).

`MLVM_CEPHYR_LIBC=ON` is the default. `MLVM_CEPHYR_SYSROOT` overrides it;
`MLVM_CEPHYR_LIBC=OFF` with no external path builds a compiler with no default
sysroot. Bundled artifacts live in `build/cephyr-sysroot` and install under
`share/mlvm/cephyr-libc`. Installed tools discover that directory relative to
their executable, supporting a moved installation prefix.

| Option | Effect |
| --- | --- |
| `--sysroot=DIR` | Select standard headers and runtime artifacts |
| `-nostdinc` | Omit standard headers; retain user `-I` paths and runtime linking |
| `--no-default-sysroot` | Clear the default root |
| `-nostartfiles` | Omit automatic `crt0.o` |
| `-nodefaultlibs` | Omit automatic `libc.a` |
| `-nostdlib` | Omit both startup and default library |

User `-I` directories precede sysroot headers. Preprocessing APIs use the same
defaults through `default_sysroot()`, `PreprocessOptions::sysroot`, and
`PreprocessOptions::standard_includes`. Embedders outside an installed tool's
directory can set `sysroot` explicitly; installed CMake packages export the
relocated path as `MLVM_CEPHYR_SYSROOT`. `MLVM::CephyrLibc` exports the runtime
archive independently of the frontend. See [libc coverage](../cephyr/libc/README.md).

## Pipeline and APIs

```text
preprocess → cparser-iso11 AST → semantic checks → MLVM builtin/arith/func/c
           → optional Optilisp kernels → MLVM passes → x86-64 → ELF64
```

The same stages are available as scheduler actions: `p:cephyr` lowers one C
translation unit into a pipeline, and the x86-64 `p:target` route emits
C-dialect modules through the C backend (see the [scheduler manual](../manual/41-schedulers.md#c-pipelines)).

`MLVM::Cephyr` exports `cephyr/frontend.hpp`: `preprocess`, `parse`, `lower`,
and `emit_assembly`. `lower` returns an owned, verified `mlvm::Module` borrowing
its context. Parser types are private. Errors return `false`/`nullopt` and are
recorded in the caller's `DiagnosticEngine`; malformed input does not terminate
the embedding process or print to stdout.

`mlvm/c.hpp` provides C-dialect registration/verification through `MLVM::IR`
and `emit_c_x86_64` through `MLVM::Codegen`. The `c` operations represent local
loads/stores, addresses, indirect memory accesses, scaled pointer offsets, casts,
global data, calls/declarations, comparisons, shifts, structured conditionals,
loops, region yields and loop transfers. They are registered by
`register_builtin_dialects`, so emitted textual IR can be verified, round-tripped,
and optimized by the `mlvm` tool. Verification checks call signatures, local-frame
ownership, integer operand/result types and control-region contracts.

The backend assigns frame slots to SSA values and locals, passes up to six integer
arguments in System V registers, aligns call frames to 16 bytes, and preserves
values across recursive/external calls. Constant-return leaves retain compact
`mov`/`ret` output. General instructions are emitted as encoded `.byte` directives
with assembly comments, accepted by both MLVM's assembler and system assemblers;
calls remain symbolic relocations. Register allocation and instruction scheduling
are not implemented for this path.

## Implemented language

| Area | Support |
| --- | --- |
| Functions | Integer/pointer/`void` results, declarations/definitions, recursion, direct calls, up to six scalar parameters, `static`/`extern` linkage |
| Objects | Signed/unsigned 8/16/32/64-bit integers, `_Bool`, object pointers, fixed-size arrays, globals, static locals, scalar/array initialization, typedefs, block scopes |
| Expressions | Decimal/octal/hex literals with U/L/LL suffixes, character/string literals, unary/binary operators, signed/unsigned arithmetic, casts, `sizeof`, address/dereference, indexing and pointer arithmetic |
| Control | `if`/`else`, `while`, `do`, `for` (including declaration initializer), `break`, `continue`, return, short-circuit `&&`/`\|\|`, conditional `?:` |
| C11 | File/block `_Static_assert` with integer constant-expression checking; `_Generic` with signed-int/default associations, unevaluated control and unselected arms |
| Diagnostics | Duplicate/conflicting declarations, undeclared identifiers/calls, bad argument counts, invalid returns, const modification, misplaced transfers, constant overflow/division by zero/invalid shifts |
| Preprocessing | Three engines (see below); `-D`/`-U`, `#undef`, includes, `-I`, forced includes in all engines |

Predefined macros: `__CEPHYR__=1`, `__STDC__=1`, `__STDC_VERSION__=201112L`, `__STDC_HOSTED__=0`.
The bundled headers use `__CEPHYR__` to expose callable scalar/pointer APIs while
retaining host-compiler declarations for unsupported types and calling conventions.
Right shift of negative signed integers uses arithmetic shift. An integer function
falling through returns zero in this implementation; only `main` receives that
guarantee from the C language itself.

## Preprocessing engines

```sh
build/cephyr -E examples/answer.c                                   # ucpp (default)
build/cephyr -E --preprocessor=internal examples/answer.c
build/cephyr -E --preprocessor=system examples/answer.c             # auto-detected cc
build/cephyr -E --preprocessor=system --system-preprocessor=gcc-14 examples/answer.c
```

`--preprocessor=internal|ucpp|system` selects the engine; `--system-preprocessor`
names one executable (bare names resolve via `PATH`, default `cc`, `gcc`,
`clang`, then `cpp`). The API equivalents are `PreprocessOptions::engine` and
`PreprocessOptions::system_command`.

| Engine | Implementation | Coverage |
| --- | --- | --- |
| `ucpp` (default) | Vendored `third_party/ucpp` linked as a library with structured diagnostics | Full C99 preprocessing: function-like and variadic macros, `#`/`##`, general `#if` expressions, `#elif`, `#include`/`#include_next`, `#line`, `#error`, `#pragma` passthrough |
| `system` (opt-in) | External GCC-compatible preprocessor subprocess (`-E -P -undef -std=c11 -ffreestanding -nostdinc` plus user `-I`/`-D`/`-U`/`-include`) | Host preprocessing behavior; no shell is used and only the named executable runs |
| `internal` | Minimal deterministic in-tree engine | Object-like macros, recursive-expansion suppression, line splicing, comments, quoted tokens, `-D`/`-U`, `#undef`, includes, `#ifdef`/`#ifndef`, `#if 0/1`, `#else`/`#endif`, `#error` |

All engines enforce the freestanding predefined macros above and a 64 MiB
output limit. Include nesting is bounded at depth 64 for the `internal` and
`ucpp` engines (cycles are diagnosed, including recursion that would otherwise
rely on guard handling); the `system` engine uses the host's own limit.
Undefining ucpp's special macros (`__STDC__`, `__FILE__` and similar) is
rejected; the internal engine permits it. System-preprocessor warnings (such as
`#warning`) surface as warnings without failing the run, and its failures report
the driver's exit status with captured output. Overwrite protection covers the
main input in every engine; transitive include tracking is internal-only.
Includes and splices shift locations in every engine: frontend diagnostics refer
to the `<preprocessed>` buffer, and exact original macro/include provenance
mapping remains pending.

## Commands and kernels

```sh
build/cephyr -std=c11 -fsyntax-only examples/answer.c
build/cephyr --emit-ir examples/answer.c -o build/answer.mlvm
build/mlvm --verify build/answer.mlvm
build/cephyr -S examples/answer.c -o build/answer.s
build/cephyr -c examples/answer.c -o build/answer.o
build/cephyr --emit-ir --apply-kernel=KERNEL-ID examples/answer.c
build/cephyr -i examples/answer.c -o build/answer.mlvm
build/cephyr -s examples/answer.c -o build/answer.s
```

### Dialect-based target emission

Translation units that lower to pure integer expressions (no parameters,
locals, globals, calls, or control flow — `examples/answer.c` qualifies) can
also emit through the shared dialect backends instead of the C backend:

```sh
build/cephyr --target=llvmir-unknown-unknown examples/answer.c -o build/answer.ll
build/cephyr --target=wasm32-unknown-unknown examples/answer.c -o build/answer.wasm
build/cephyr --target=interp-unknown-unknown examples/answer.c
build/cephyr --target=interp-unknown-unknown --args=35 param.c
```

`--target` emission accepts one source and cannot be combined with `-E`,
`--parse-only`, `-fsyntax-only`, `--emit-ir`, `-S`, or `-c`. Text outputs
default to stdout; the WebAssembly binary requires `-o`. `--args` (or
`--arguments`) supplies comma-separated interpreter arguments for the
`interp` target only. Modules using C memory, address, call, or control
operations fail with an explicit diagnostic naming the blocking operation;
compile those through the C backend (`-S`/`-c`/linking) instead. The
`x86_64-unknown-linux-gnu` triple keeps its existing assembler-selection
meaning.

### Assembler and linker selection

Cephyr uses MLVM's assembler and linker **in process by default**, sharing the
implementations behind `mlvm-as` and `mlvm-ld`. Set `CEPHYR_AS` / `CEPHYR_LD` to
select external tools, or override either with `--assembler COMMAND` /
`--linker COMMAND` (also accepted as `--assembler=COMMAND` / `--linker=COMMAND`).
CLI selection takes precedence over the environment. Empty environment variables
use the default; the reserved command `mlvm` explicitly restores the built-in tool.

```sh
CEPHYR_AS=as CEPHYR_LD=ld build/cephyr main.c helper.c -o program
build/cephyr --assembler=/opt/bin/my-as --linker=/opt/bin/my-ld main.c -o program
build/cephyr --assembler=mlvm --linker=mlvm main.c -o program
```

An external command names one executable, resolved through `PATH` for bare names.
It is not a shell command or a command-plus-flags string; use a wrapper for extra
arguments. External execution currently requires POSIX. The invocation contracts are:

* Assembler: `COMMAND -o OUTPUT.o INPUT.s`; output must be x86-64 ELF relocatable.
* Linker: `COMMAND -o OUTPUT -e _start CRT0.o INPUT... LIBC.a`;
  startup and libc are omitted according to the runtime flags above. Objects and
  archives retain their input order. The tool must produce a runnable executable
  compatible with the chosen runtime.

Both built-in and external stages use the selected sysroot. Temporary assembly,
objects, and external-tool output are cleaned up; final output is written only
after successful tool execution. Child diagnostics go to the terminal and a
nonzero child status is returned by Cephyr. No assembler is invoked for `-s`/`-S`
or `-i`/`--emit-ir`; no linker is invoked for `-c`.

### Optimizer selection

`--kernel-file=FILE`, `--apply-kernel=ID`, and `--blackstone-dir=DIR` use the same
Blackstone/Optilisp integration as `mlvm`, including user-authored kernels. Kernel
execution requires Optilisp enabled and a code-producing mode (`--emit-ir`, `-S`,
`-c`, or default executable linking). API clients can apply `mlvm::optilisp::run_kernels` to the lowered module;
textual IR supports the declarations/applications described in [KERNELS.md](KERNELS.md).

`-fKERNEL-ID` selects a Blackstone or previously loaded custom kernel by its
declared ID. `-fFILE.olisp` loads and applies every kernel declared in that file,
in stable ID order; `-f FILE.olisp` is also accepted. File selections are recognized
by a `.olisp` suffix, a path separator, or an existing file. Repeated file selections
load the definitions once and apply them again at each selection position.

`--optimize FILE.json` (or `--optimize=FILE.json`) reads a plan:

```json
{
  "kernel_files": ["kernels/my-identities.olisp"],
  "kernels": [
    "blackstone.hir.algebraic-simplification.add-zero",
    "blackstone.hir.custom.my-identity",
    {"file": "kernels/other-identities.olisp"}
  ]
}
```

`kernels` is a required ordered array of IDs and/or `{"file":"..."}` entries.
`kernel_files` is an optional array of files to load without automatically applying
them, useful for selecting just one definition from a file. All plan file paths
are relative to the JSON file's directory; direct CLI paths are relative to the
working directory. Unknown fields, duplicate fields, wrong types, malformed JSON,
empty strings and NULs are rejected. Plans have a maximum nesting depth of 32.

Plans, `-f` selections, and `--apply-kernel` requests can be repeated and mixed;
application order follows command-line order and each plan's `kernels` array.
Each source translation unit receives the same selections before code generation.
Catalog and custom definitions undergo the existing Optilisp validation and
backend applicability checks; this interface does not expand the executable
kernel subset documented in [KERNELS.md](KERNELS.md).

```sh
build/cephyr -fblackstone.hir.algebraic-simplification.add-zero main.c -o program
build/cephyr -fkernels/my-identities.olisp -i main.c -o main.mlvm
build/cephyr --optimize optimize.json main.c helper.c -o program
```

`--parse-only` checks the adapted parser without semantic lowering.
`-fsyntax-only` includes semantic validation. `-s` aliases `-S` (assembly), and
`-i` aliases `--emit-ir` (MLVM textual IR). These and `-E` default to stdout;
use `-o FILE` to write a file. `-c` emits unlinked ELF (`ET_REL`), defaulting to
the source basename with `.o` in the working directory. These explicit modes take one source input.
Default linking accepts multiple `.c`, `.o`, and `.a` inputs and writes `a.out`
unless `-o` is specified. Archives are searched in command-line order, with
the default runtime searched last. `-O0`/`-O1` use the same baseline passes.

## Remaining C11 work

Floating-point/complex types, structs/unions, function pointers, type-rank
distinctions between LP64 `long` and `long long`, complete qualifier checking,
inferred/variable array bounds, non-null static pointer initializers, alignment
operators, designated initializers, varargs, atomics, thread-local storage,
`switch`/labels/`goto`, inline semantics, and statements after unconditional
transfers are not implemented. Unsupported constructs are diagnosed. Upstream
parser coverage alone does not establish C11 semantic acceptance.

The `internal` engine still lacks function macros, `#`/`##`, general `#if`
expressions, `#elif`, `#line`, pragmas and full translation-phase handling;
prefer `ucpp` for those. Internal expansion limits are depth 128, 16 MiB per
line, include depth 64, and output 64 MiB. System-preprocessor sources must additionally stay
within Cephyr's scalar/pointer semantic subset: unsupported types and GNU extensions in
the taken branch are diagnosed at lowering time.

Regression tests cover parser recovery in embedding, malformed input, C11
assertions/generic selection, IR round trips and invalid contracts, native
compile/assemble/link/execute cases, and host-compiler System V ABI interoperability.
