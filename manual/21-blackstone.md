# 21. Blackstone kernels

[Contents](README.md) · [Next: Kernel CLI](22-kernel-cli.md)

MLVM integrates the current `libstone/blackstone` **`.bst` language and native C
rewrite runtime** through `MLVM::Scheduler`. Lua `.sched` files describe when
kernels run and how their output flows into subsequent MLVM/Firestone stages.

The build uses Blackstone's own DParser grammar to generate a native parser.
MLVM converts that concrete syntax tree into Blackstone kernel IR, then calls
`bs_profile_compile` for declaration resolution, inheritance and validation.
Execution calls `bs_optimize`; it does not translate kernels into Lua or Python.

```sh
cmake -S . -B build
cmake --build build --parallel
build/mlvm-sched examples/sched/algebra.sched --trace
```

The actual `kernels/expressions/AlgebraicSimplification.bst` fixture contains
16 local rules and inherits built-in `ConstantFolding`. The example transforms
`(x + 3) + 4` into `x + 7`, then interprets the pipeline with `x = 35` to print 42.

## Catalog and profiles

The source catalog is `libstone/blackstone/kernels`; installed kernels are in
`share/mlvm/blackstone/kernels`. Kernel identity comes from the `Kernel` declaration.
The loader registers the supplied expression and structured descriptor contracts.
The native compiler checks the complete supplied catalog in the integration tests.

The built-in MLVM and Firestone adapters execute the integer expression domain.
Statement, memory, control-flow, loop and polyhedral descriptor transformations
require host adapters supplying their semantics and proven capabilities. A known
capability name does not make its predicate true.

Compiled profiles can be saved as `.bsp` and reloaded through Blackstone's
versioned, validated binary format. A profile owns its kernel instructions;
`Kernel` owns both the profile and its registry.

## Migration from Optilisp

The former `.olisp` path is a legacy optional component, disabled by default and
requiring its separately available sources. `.sched` orchestration uses `.bst`
kernels directly; old textual `kernel`/`apply` directives and old CLI kernel
flags are not the scheduler API. Use a `kernel` node in an orchestrator.

See [schedulers](41-schedulers.md), [native C++ API](23-kernel-api.md), and
[Blackstone's native API](../libstone/blackstone/docs/native-api.md).
