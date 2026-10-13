# mlvm

Compiler/IR driver: parse IR, verify, run pass pipelines, convert dialects, emit IR or target output.

```sh
build/mlvm input.mlvm -o output.mlvm
build/mlvm input.mlvm --passes=constant-fold,dce --convert-to=low -o lowered.mlvm
build/mlvm input.mlvm --target=llvmir-unknown-unknown -o output.ll
build/mlvm input.mlvm --target=wasm32-unknown-unknown -o output.wasm
build/mlvm input.mlvm --target=x86_64-unknown-linux-gnu -o output.o
build/mlvm input.mlvm --target=interp-unknown-unknown --args=35
```

`--convert-to` accepts `high|mid|low|machine|interp|llvmir|wasm|gpu` and runs the
standard `arith -> high -> mid -> low -> target-facing dialect` lowering
transactionally: failure leaves the input unchanged and reports a diagnostic.
`--target` emits `llvmir-unknown-unknown` (LLVM text), `wasm32-unknown-unknown`
(binary module), `x86_64-unknown-linux-gnu` (ELF64 object), or
`interp-unknown-unknown` (decimal result text). `--args`/`--arguments` supplies
comma-separated unsigned decimals for the interpreter only. Passes run before
conversion; conversion runs before emission. `--verify` validates (including a
requested conversion) without writing output; binary targets require `-o FILE`.
Direct `--convert-to`/`--target`/`--passes`/`--verify`/`--print` options cannot be
combined with `--schedule` orchestration; use scheduler `convert`/`target` actions
there instead (see `manual/41-schedulers.md`).
