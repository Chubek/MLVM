# Chapter 17 — Scheduling and convergence

Blackstone uses a deterministic ordered driver. It resolves kernels base-first,
uses source order within each kernel, visits operand subtrees before their parent
and chooses the first matching non-identity rule.

After a successful rewrite, the driver revisits replacement children and restarts
the entire combined rule list on the replacement. It continues to a fixed point;
one pass through the local rules is not the execution contract.

## A three-step cleanup

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/wrapping-cleanup.json --trace
```

Unsigned-3-bit arithmetic follows this sequence:

```text
((x plus 3) plus 5)
    -> (x plus (3 plus 5))       ReassociateAddConstants
    -> (x plus 0)                ConstantFolding, modulo 8
    -> x                         AddZeroRight
```

The inherited folder runs again on the newly built child. Its result then enables
a local identity because the combined list restarts. The final node retains `x`'s
identity and exact unsigned-3-bit type.

## Shared nodes

Normalization memoizes host value identities within a run. A shared child is
normalized once and reused at its occurrences, producing a deterministic trace
without losing identity needed by `!same`. This is memoization of compiler
normalization, not of source-program evaluation.

An already normalized output is idempotent: optimizing it again preserves its
identity and produces an empty trace. The example runner checks this property.

## The acceptance termination measure

For the reference occurrence tree, use the lexicographic measure `(B, L, D)`:

1. `B` counts binary nodes.
2. `L` counts `plus` nodes with literal-left/nonliteral-right operands.
3. `D` sums left-subtree sizes at `plus`/`times` nodes whose right operand is
   literal; subtree size includes literals and inputs.

Identities, annihilators and folding decrease `B`. Literal-right normalization
preserves `B` and decreases `L`. Reassociation preserves both and decreases `D`.
The supplied rules have no reverse reassociation rule. For an acyclic shared
graph, the conceptual unfolded tree gives the corresponding argument.

## Arbitrary kernels and budgets

The deliberately unoriented swap example passes static validation but has no
canonical direction:

```sh
python3 -m blackstone apply examples/kernels/Swap.bst \
  examples/swap-input.json --budget 4 --trace
```

After four swaps, the next candidate triggers nonconvergence. The CLI publishes
no optimized JSON and reports completed trace events plus the pending/recent rule
names. Budget counts successful rewrites; identity/no-change candidates do not
spend it. The default is 10,000.

The native driver also enforces a maximum expression depth of 256 by default.
C options can lower that bound. Cyclic expression input is rejected. A mutable
SSA host additionally schedules affected users through its def-use machinery,
as chapter 24 explains.

[Previous](16-hir-fixtures-and-provenance.md) · [Contents](README.md) · [Next: CLI and diagnostics](18-command-line-and-diagnostics.md)
