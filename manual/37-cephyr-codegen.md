# 37. Cephyr semantics, lowering, and code generation

[Contents](README.md) · [Next: End-to-end workflow](38-end-to-end.md)

Cephyr's implemented C11 slice supports signed/unsigned 8/16/32/64-bit integers,
`_Bool`, pointers, fixed arrays, globals/static locals, typedefs, casts and `sizeof`.
Functions take up to six scalar parameters and return scalars or void. Direct
calls/recursion, block scopes, arithmetic, comparisons, short-circuit expressions,
and structured conditionals/loops are supported.
`_Static_assert` and signed-int/default `_Generic` associations are supported.
The complete boundary is recorded in [CEPHYR.md](../docs/CEPHYR.md).

```sh
build/cephyr -std=c11 --emit-ir examples/answer.c -o build/answer.from-c.mlvm
build/cephyr -S examples/answer.c -o build/answer.from-c.s
build/cephyr -c examples/answer.c -o build/answer.o
build/cephyr main.c helper.c -o build/program
build/cephyr --target=llvmir-unknown-unknown examples/answer.c -o build/answer.ll
build/cephyr --target=interp-unknown-unknown examples/answer.c
```

Lowering produces registered `builtin`, `arith`, `func`, and `c` operations.
Local variables use function-owned slots; expression values use SSA. Structured
regions preserve short-circuit evaluation and loop transfers. MLVM verifies types,
dominance, call signatures, slots and yields before code generation.

Translation units that lower to pure integer expressions reach the shared
dialect backends through `--target`: LLVM text, WebAssembly binaries, or
evaluated results. Parameters, locals, globals, calls, and control flow lower
to C operations with no expression lowering, so those modules fail `--target`
emission with an explicit diagnostic naming the blocking operation and must use
the System V backend below.

The System V backend assigns frame slots to SSA values and locals, maintains
16-byte call alignment, passes arguments in registers, and returns integers in
`%eax`/`%rax`. Constant-return functions retain `mov`/`ret` output. Other instructions
use encoded `.byte` directives with comments; calls use symbolic relocations.
Both MLVM's assembler and system assemblers accept the generated assembly.

Checked C constant evaluation diagnoses signed overflow and invalid arithmetic.
It is separate from modular generic-IR folding. `-O0`/`-O1` currently share baseline
passes. With Optilisp enabled, `--kernel-file`, `--apply-kernel`, and
`--blackstone-dir` apply Blackstone or user-authored kernels before emission.
API consumers can apply the same kernel APIs to the module returned by `lower`.

Explicit compilation modes accept one source; conflicting modes are invalid usage.
`-E`, `-S`, and `--emit-ir` default to stdout; `-c` requires `-o`. `--target`
emission likewise accepts one source and conflicts with every explicit mode:
LLVM/interp text defaults to stdout while WebAssembly requires `-o`. Default linking
accepts multiple C sources, objects and archives, adds the selected sysroot's
`crt0.o`/`libc.a`, and emits `a.out` or the requested `-o` path. `-nostdlib`,
`-nostartfiles`, and `-nodefaultlibs` control automatic runtime inputs.
