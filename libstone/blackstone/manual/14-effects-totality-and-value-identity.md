# Chapter 14 — Effects, totality and value identity

An algebraic equation does not describe all observable behavior of an expression.
Rules must also preserve evaluations, effects, exceptional behavior and defined
values under the host's evaluation model.

## Pure and total are separate facts

`Pure($n)` certifies referential transparency: evaluation is stable and has no
observable effects or nondeterminism. Volatile/atomic accesses, repeated
state-dependent loads and effectful calls are not certified pure merely because
their returned values look alike.

`Total($n)` certifies that evaluation terminates and produces a defined value
for all inputs admitted by the current facts. It excludes exceptions, traps,
undefined behavior, poison and independently varying undef values.

A deterministic division might be pure and still potentially trap. A divergent
computation might have no observable writes and still fail totality. The
[`trapping-zero`](../examples/trapping-zero.json) fixture explicitly demonstrates
`pure: true`, `total: false`, which blocks discarding its operand.

## Retaining versus erasing

Compare:

```text
x plus 0  -> x     retains one evaluation of x
x times 0 -> 0     removes an evaluation of x
x minus x -> 0     removes two operand occurrences
```

The first identity can retain an effectful input without an erasure proof. The
other two require facts that justify removing evaluations. Self-subtraction also
requires both operands to have the same stable identity.

```sh
python3 examples/run_examples.py --engine both \
  --case add-zero-right --case multiply-zero --case effectful-zero
```

Constant movement and reassociation can retain effectful captures because they
move only concrete literals around them. The acceptance rules preserve the
number and observable relative order of nonliteral evaluations.

## Identity is not textual equality

The reference adapter uses expression-node identity. A repeated JSON node ID
denotes the same value; two separate IDs with the same descriptive `op` do not.
Even structurally identical binary subtrees are distinct if their node identities
are distinct. `!same` is stronger than equal-looking source text.

Shared normalization preserves that distinction. In
`examples/shared-normalization.json`, both operands reference the same addition
node. The driver normalizes it once, reuses the normalized identity at both
occurrences and then permits guarded self-subtraction.

## Occurrence trees and SSA

In the reference model, every operand occurrence represents an evaluation.
Serialization sharing establishes identity; it does not itself memoize source
execution or prove purity. Therefore two occurrences of one effectful tree input
still denote two evaluations.

An SSA reference instead reads an already-produced value. Its purity/totality
facts describe that read and its definedness, not a demand to rerun the defining
instruction. An SSA integration must keep effectful definitions and effect
dependencies after rewritten uses become dead. Host dead-code elimination needs
the same evaluation model as the rewrite proofs.

**Exercise:** change the shared input's totality fact in `subtract-self.json` to
`false`. The two identities are still equal, but the cancellation must decline.
Identity alone does not justify removing a trap or undefined value.

[Previous](13-operators-and-constant-folding.md) · [Contents](README.md) · [Next: Replacement construction](15-replacement-construction.md)
