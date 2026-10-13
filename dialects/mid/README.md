# Mid-level SSA dialect

`mid` implements the [common integer expressions](../README.md). Values use
MLVM's stable SSA identity and checked use-def chains. `mid.select` takes an i1
condition and two values whose type exactly matches its single result.

The standard route lowers `high` expressions to `mid`, then to `low` while
preserving types, source locations, attributes and operand identity. Firestone
`SimpleSSAGraph` is available through the scheduler for explicit CFG/phi-based SSA.
