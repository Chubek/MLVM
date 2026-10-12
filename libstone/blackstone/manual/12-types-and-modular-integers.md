# Chapter 12 — Types and modular integers

Every matched binary has a concrete result type. A valid replacement preserves
that exact type, including its domain, width and signedness. `-> Term` and
`-> Binary` are shape declarations, not permission to widen, reinterpret or cast
the arithmetic result.

## Integer ranges

For width `W >= 1`:

| Interpretation | Representable numeric values |
| --- | --- |
| Unsigned | `0` through `2^W - 1` |
| Signed | `-2^(W-1)` through `2^(W-1) - 1` |

Addition, subtraction and multiplication wrap modulo `2^W`. The signed
interpretation assigns the high half of the bit representations negative numeric
values. Thus signed-8-bit `120 plus 20` is `-116`, while unsigned-8-bit arithmetic
would represent the same bits as `140`.

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/signed-wrap.json --trace
```

The constant folder retains signed width 8. It does not compute at a host machine
width and leave an out-of-range literal in HIR.

## The ModularInteger contract

The predicate requires all operands/results of every explicitly matched Binary
to use one homogeneous scalar integer type. It rejects floating-point, pointer,
Boolean and vector domains, mismatched widths/signedness, unresolved values and
operators whose flags change the registered semantics.

Saturating, checked-overflow and overflow-poison variants are excluded. The
reference implementation conservatively rejects every nonempty semantic-flag
list, including `nsw` and `nuw`.

```sh
python3 examples/run_examples.py --engine both \
  --case mixed-types --case nested-overflow-flag
```

The second case has a flagged inner addition and an unflagged outer addition.
Checking only the root would miss the changed semantics; the nested signature
exposes both operators, so reassociation declines.

## Captured interiors and literal construction

Opaque captured Term subtrees keep their own internal types and semantics.
`ModularInteger` does not impose wrapping semantics recursively on every hidden
operator. Its immediate-operand type check still requires each captured value's
result type to agree with the matched skeleton.

A replacement integer such as `0` uses the original root's exact type. Literal
input spellings must already be representable; the fixture loader does not wrap
out-of-range spellings. Integer literals in the reference JSON format are concrete
integers. Other domains can be represented with input/binary metadata, while
richer literal kinds require an appropriate host adapter.

## Wide integers

Source integer spelling and reference arithmetic are arbitrary precision:

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/wide-integer.json --engine native --trace
```

The unsigned-128-bit maximum plus one becomes unsigned-128-bit zero. The native
bridge delegates arithmetic to its host adapter; the runtime does not truncate
these values to `uint64_t`. A direct C host supplies the same exact-typed contract.

[Previous](11-guards-and-analysis-facts.md) · [Contents](README.md) · [Next: Operators and folding](13-operators-and-constant-folding.md)
