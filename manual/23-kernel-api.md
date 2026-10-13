# 23. Applying kernels through C++

[Contents](README.md) · [Next: Authoring kernels](24-authoring-kernels.md)

Link `MLVM::Scheduler` and include `<mlvm/sched.hpp>`. A verified MLVM module
can be optimized directly with the same native kernel used by a `.sched` node:

```cpp
auto kernel = mlvm::sched::Kernel::load(
    "libstone/blackstone/kernels/expressions/AlgebraicSimplification.bst",
    {}, diagnostics);
std::vector<mlvm::sched::Event> trace;
if (kernel) {
    bool ok = mlvm::sched::apply(*kernel, module, 10000, trace, diagnostics);
    // module and trace change only after successful optimization/verification.
}
```

The search-directory argument is a `span<const filesystem::path>`. It supplies
inheritance dependencies; missing directories and duplicate kernel declarations
are errors. Loading and execution are separate operations. `kernel->rules()`
returns the resolved native rule order, including inherited constant folding.

`apply` also accepts `mlvm::sched::Dataflow`, which owns Firestone's actual
`DataflowGraph` and output roots. The adapter inspects typed nodes, calls native
matching/guard/construction callbacks, and publishes new roots after verification.

## Facts and SSA identity

MLVM uses exact SSA value identity, not structural equality. Opaque SSA references
are reads of already-produced values. Their totality is unknown by default.
An optional `mlvm::sched::Facts` callback supplies `BS_TRUE`, `BS_FALSE` or
`BS_UNKNOWN` for `Pure` and `Total` on those references. Supply actual host proofs;
unknown facts block guards that need them.

Original defining operations, effects and potential traps remain allocated;
replacement updates uses and rebuilds use-def information. New operations
preserve result types, source locations and originating rule metadata.

## Ownership and profiles

`Kernel` owns its registry and immutable native profile. `profile()` exposes a
borrowed `const BSProfile*` for host adapters. Use `<mlvm/blackstone.hpp>` for
the C ABI from C++20: it adapts the C member named `requires` without changing
ABI layout. `write_profile(path, diagnostics)` and `read_profile` round-trip
Blackstone's validated `.bsp` format.

MLVM `Context` remains borrowed and must outlive modules, scheduler registries,
graphs and result artifacts. The scheduler supports whole-run transactional
publication; see [embedding](41-schedulers.md#c-embedding).
