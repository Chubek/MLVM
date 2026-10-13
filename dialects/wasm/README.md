# WebAssembly dialect

`wasm` implements the [common integer expressions](../README.md) restricted to
i32/i64, plus `wasm.drop`. Binary operands/results must have identical types.
Overflow flags are rejected. `wasm.select` uses an i32 condition and two matching
i32/i64 values; `drop` accepts one i32/i64 value and produces no result.

`emit_wasm` writes a version-1 binary module with type, function, export and code
sections. Function parameters and SSA definitions map to WebAssembly locals.
Every function is exported by its `sym`. Constants use signed LEB128 and lengths
use unsigned LEB128. The expression emitter supports arithmetic, selection,
copies and returns; unsupported operations are diagnosed. Integration tests
validate and execute the binary using Node's WebAssembly implementation.
