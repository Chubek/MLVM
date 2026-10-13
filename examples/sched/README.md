# Lua orchestrators

Build MLVM, then run from the repository root:

```sh
build/mlvm-sched examples/sched/algebra.sched --trace       # prints 42
build/mlvm examples/sched/firestone.sched                  # 3AC and SSA graph
build/mlvm-sched examples/sched/dependencies.sched --dot   # pipeline DOT
build/mlvm-sched examples/sched/targets.sched --result=native -o build/pipeline.o
build/mlvm-sched examples/sched/targets.sched --result=wasm -o build/pipeline.wasm
build/mlvm-sched examples/sched/targets.sched --result=llvm -o build/pipeline.ll
```

`input.mlvm` computes `(x + 3) + 4`. The actual Blackstone algebra kernel
reassociates it to `x + (3 + 4)` and inherited constant folding produces `x + 7`.
The interpreter example calls it with `x = 35`.

All file paths in action options are relative to the `.sched` file. Installed
examples are adjusted during installation to use the installed
`share/mlvm/blackstone/kernels` catalog. When copying an example elsewhere,
update its paths to point to your own IR and kernel files.

See [the scheduler manual](../../manual/41-schedulers.md) for the complete DSL.
