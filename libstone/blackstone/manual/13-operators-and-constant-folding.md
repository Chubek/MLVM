# Chapter 13 — Operators and constant folding

Backtick names resolve to registered HIR operators. They are not host-language
functions to invoke while parsing a rule. The acceptance integer meanings are:

| Operator | Meaning at width `W` |
| --- | --- |
| `plus` | Addition modulo `2^W` |
| `minus` | Subtraction modulo `2^W` |
| `times` | Multiplication modulo `2^W` |
| `divide` | Unsigned quotient, or signed quotient truncated toward zero |
| `bit_and` | AND on the same `W`-bit representation |
| `bit_or` | OR on the same `W`-bit representation |
| `bit_xor` | XOR on the same `W`-bit representation |

For a signed result, a bitwise operation's representation is interpreted with the
original signed type. Numeric literal construction and bit representation remain
distinct concepts: unsigned-8-bit all ones is `255`, while signed-8-bit all ones
is `-1`.

## The inherited folder

`ConstantFolding` is one ordered built-in entry handling the seven operators.
It recognizes eligible literal/literal binaries and constructs a literal at the
same exact type. It is installed when inherited and no source kernel defines
that name. It contributes one resolved entry, not another local fixture rule.

Folding revisits the driver just like any other successful rewrite. A folded zero
or one can immediately enable local identities or annihilators. Reassociation
creates a literal/literal subtree rather than calculating its result inside the
rule body.

## Exceptional division

Division by zero is exceptional. Signed minimum divided by negative one is also
exceptional because its positive quotient is not representable. The folder
declines these operations and leaves the binary node intact:

```sh
python3 examples/run_examples.py --engine both \
  --case signed-division --case divide-by-zero --case signed-division-overflow
```

Signed `-7 divide 3` becomes `-2`, not the floor quotient `-3`. A nonfolded
exceptional expression is a successful optimization run with no rewrite, not a
Blackstone evaluation failure: the source program has not been executed.

## The 16-rule acceptance inventory

All rules require `ModularInteger`; additional conditions are listed here.

| Rule(s) | Transformation | Additional condition |
| --- | --- | --- |
| `AddZero` | `0 + x -> x` | Root associativity retained from the seed |
| `AddZeroRight`, `SubtractZero` | `x + 0`, `x - 0 -> x` | None |
| `SubtractSelf` | `x - x -> 0` | Same identity; pure, total `x` |
| `MultiplyOne`, `MultiplyOneRight` | `1 * x`, `x * 1 -> x` | Positive one is representable |
| `MultiplyZero`, `MultiplyZeroRight` | `0 * x`, `x * 0 -> 0` | Discarded `x` is pure and total |
| `DivideOne` | `x / 1 -> x` | Positive one is representable |
| `AndZero` | `x & 0 -> 0` | Discarded `x` is pure and total |
| `AndSelf` | `x & x -> x` | Same identity; pure, total `x` |
| `OrZero` | `x \| 0 -> x` | None |
| `XorSelf` | `x ^ x -> 0` | Same identity; pure, total `x` |
| `AddConstantRight` | `c + x -> x + c` | Commutativity; literal `c`, nonliteral `x` |
| `ReassociateAddConstants` | `(x + c1) + c2 -> x + (c1 + c2)` | Associativity; literal constants |
| `ReassociateMultiplyConstants` | `(x * c1) * c2 -> x * (c1 * c2)` | Associativity; literal constants |

**Try:** the all-ones example uses an additional catalog predicate and kernel,
showing how exact-width bitwise identities extend this starting inventory.

[Previous](12-types-and-modular-integers.md) · [Contents](README.md) · [Next: Effects and identity](14-effects-totality-and-value-identity.md)
