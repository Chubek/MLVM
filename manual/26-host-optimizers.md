# 26. Host optimizers and Firestone orchestration

[Contents](README.md) · [Next: NodeIR](27-nodeir.md)

A scheduler registry maps action names to factories. Factories validate string
options and return native actions; `.sched` nodes call those actions with
immutable predecessor artifacts. An artifact is a kind string plus an owning
`std::any` value, so hosts can orchestrate additional Firestone representations
without changing the Lua parser.

```cpp
registry.add("host.ssa-pass", [](const mlvm::sched::Options& options) {
    if (!options.empty()) throw std::invalid_argument("this pass takes no options");
    return [](std::span<const mlvm::sched::Artifact* const> inputs,
              mlvm::sched::RunContext& run) -> std::optional<mlvm::sched::Artifact> {
        if (inputs.size() != 1) throw std::invalid_argument("expected one input");
        auto program = inputs[0]->get<firestone::ir::SimpleSSAGraph>();
        // Run a host transformation or Firestone PassManager on this owned copy.
        mlvm::sched::Artifact output{"firestone.ssa", std::move(program)};
        if (!mlvm::sched::verify_artifact(output, run.diagnostics)) return {};
        return output;
    };
});
```

Lua invokes this with `p:node{name="host", action="host.ssa-pass",
inputs={"ssa"}}`. The scheduler verifies known artifact kinds again at
publication. Custom kinds need custom verification before the action returns.

## Native Blackstone adapters

Use `Kernel::profile()` with `bs_optimize` and a `BSAdapter` to implement custom
typed trees or descriptors. The adapter supplies inspection, identity,
equivalence, predicate queries, immutable construction, rebuilding, folding and
trace callbacks. Values and strings must remain alive for the whole native run.

Capture identity and structural equivalence are different contracts. Guard
queries inspect metadata and analysis facts; they do not evaluate source code.
Never let a C++ exception escape a callback through the C runtime. Record the
failure and return the callback's documented failure value.

SSA hosts also implement installation, use-def updates and effect-aware dead-code
elimination. The supplied MLVM adapter preserves original definitions and updates
uses transactionally. Structured control/memory/loop kernels need proofs of the
complete transformation, not just matching syntax.

The scheduler commits the result map only after a complete successful DAG run.
Use immutable inputs and owning outputs; shared mutable application state and
external side effects are outside that transaction.
