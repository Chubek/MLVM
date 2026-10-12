# Chapter 10 — Term refinements

Refinements are built-in matching conditions. They do not need `requires`
declarations and do not evaluate the source program. Every Term leaf binds a
capture if the complete pattern succeeds.

| Refinement | Required condition |
| --- | --- |
| `Term!resolves` | The existing value's sort/type are resolved |
| `Term!literal` | A resolved concrete typed literal |
| `Term!nonliteral` | A resolved value that is not a concrete literal |
| `Term!exact(n)` | A concrete literal with representable numeric value `n` in its actual type |
| `Term!same($n)` | A resolved value with the same exact type and stable identity as an earlier capture |

## Resolution does not mean evaluation

`!resolves` accepts an existing typed expression without asking Blackstone to
evaluate it. Similarly, `!literal` does not mean “constant-foldable.” A binary
expression whose operands are literals is still a binary until a folding entry
replaces it.

The postorder driver can fold a child before matching its parent. That scheduling
step may produce a literal that a parent's `!literal` then recognizes. The
refinement itself only inspects the current node.

## Exact means numeric and representable

`!exact(1)` means positive numeric one. It does not mean “the low bit is set,”
and it does not reduce an unrepresentable source integer modulo the width.
A signed one-bit literal with representation `1` has numeric value `-1`; it
cannot match positive one.

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/signed-one-bit.json --trace
```

The multiplication remains unchanged. Replacing its `-1` fixture spelling with
`1` would make the literal invalid for that type; the loader diagnoses it rather
than manufacturing a wrapped literal. Source exact integers can be arbitrarily
long decimal strings, but a concrete host type still limits their numeric values.

## Nonliteral orientation

`AddConstantRight` requires a literal left operand and a nonliteral right operand.
After swapping them, the new left operand is nonliteral, so the rule cannot match
again. This is both a matching restriction and a simple convergence argument.

`!nonliteral` does not certify purity or totality. An effectful input, potentially
trapping subtree or unknown-fact value can all be resolved nonliterals.

## Same identity

`!same($n)` compares stable value identity and exact type, not source text or
structural equality. Compare these two fixtures:

```sh
python3 examples/run_examples.py --engine both \
  --case subtract-self --case distinct-values
```

The shared input in `subtract-self.json` enables the guarded self-subtraction
rule. The two separate input IDs in `distinct-values.json` remain distinct even
though both display as `x`.

Unknown refinements, arguments on argument-free refinements, missing/wrong exact
arguments and forward identity references are static diagnostics. Effect and
totality requirements are guards, not consequences of an identity refinement.

[Previous](09-captures-and-pattern-structure.md) · [Contents](README.md) · [Next: Guards](11-guards-and-analysis-facts.md)
