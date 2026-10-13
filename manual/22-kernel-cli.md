# 22. Scheduling kernels from the CLI

[Contents](README.md) · [Next: Kernel API](23-kernel-api.md)

Use `mlvm-sched FILE.sched`, `mlvm FILE.sched`, or
`mlvm --schedule=FILE.sched`. The orchestrator declares input IR, kernels,
conversions, passes and output stages.

```lua
local p = require("lsched").pipeline()
local input = p:ir("input", {path = "input.mlvm"})
local optimized = p:kernel("optimized", input, {
  path = "AlgebraicSimplification.bst",
  directory = "kernels", -- optional recursive inheritance search
  budget = "10000",
})
p:verify("checked", optimized)
return p
```

All file options resolve relative to the `.sched` file. A `.bst` path selects the
root kernel by its declaration; `directory` adds recursive `.bst` dependencies.
Dependencies resolve base-first in listed order; shared ancestors execute once.
`ConstantFolding` is built in when not provided by a source declaration.
For a compiled kernel, use a `.bsp` path instead.

```sh
build/mlvm-sched examples/sched/algebra.sched --trace
build/mlvm-sched examples/sched/algebra.sched --result=clean -o build/optimized.mlvm
build/mlvm-sched examples/sched/firestone.sched --check
build/mlvm-sched examples/sched/firestone.sched --dot -o build/pipeline.dot
```

`--check` validates Lua and graph dependencies without executing actions.
`--dot` prints that graph. `--trace` reports completed stages and actual rewrite
rules. Without `--result`, the last topologically scheduled node is published.
Binary results require `-o FILE`; textual results default to stdout.

Invalid kernel syntax, unresolved declarations, rewrite-budget exhaustion,
cyclic scheduling, invalid IR and unsupported targets return failure with
diagnostics. File output is published only after the entire run succeeds.

Complete options and action reference: [Lua schedulers](41-schedulers.md).
