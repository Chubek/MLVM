# LLVM-oriented dialect

`llvmir` implements the [common integer expressions](../README.md), `llvmir.load`
and `llvmir.store`. Memory operations require `ptr` addresses and checked
alignment. Integer operations carry exact SSA types.

`emit_llvm_ir` / scheduler target `llvmir-unknown-unknown` emits LLVM textual IR
for integer expression functions, including selection and copies. The consumer
chooses target triple and data layout. Extra semantic attributes are rejected
by this emitter so they cannot be silently dropped. Memory lowering is a host
extension. No LLVM libraries are needed to build MLVM; integration tests use
Clang when available to compile and execute the generated text.
