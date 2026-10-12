# Chapter 15 — Replacement construction

Replacement expressions contain captures, decimal integers, parentheses and
registered backtick operators. They are declarative HIR construction recipes,
not pieces of host-language source to evaluate.

## Complete proofs before building

The runtime first completes pattern matching, refinement checks and left-to-right
guard evaluation. It constructs a candidate only after all guards succeed.
Construction does not mutate the original match. The candidate is validated
before the driver installs it as the current normalized expression.

If a constructor cannot meet the contract, it can decline that candidate. In the
Python reference driver, an unrepresentable replacement integer yields no
replacement. In the C API, constructor callbacks return zero to decline; returning
an invalid value instead is an adapter error.

## Reuse a capture

Returning `$1` preserves that value's identity, exact type and provenance.
Forwarding does not create a lookalike node or overwrite the capture's existing
origin metadata. The rewrite event still identifies the rule that forwarded it.

The first-rule example demonstrates this: `KeepLeft` is in the trace, while the
effectful input remains the original value. The programmatic example verifies
identity directly with `result is x` after wrapping constant cleanup.

## Build a typed integer

An unadorned `0` uses the matched root's exact type. It does not select a default
32-bit literal type. A replacement `1` similarly must be representable, so a
signed one-bit type cannot receive a constructed positive one.

New literals inherit the source root's debug provenance and record the rule name
in `origin`. In `examples/subtract-self.json`, the new zero has signed width 8 and
origin `SubtractSelf`.

## Build a binary tree

The reassociation replacement is:

```text
$1 `plus` ($2 `plus` $3)
```

It creates two typed binary nodes. The inner literal/literal addition remains HIR
until the inherited folder runs. Both nodes use the original root type and the
registered semantics established by the match. The reference implementation
excludes altered source flags and gives newly built operators no semantic flags.
Opaque reused captures preserve their own internal nodes and flags.

For structured-host operators, construction also requires eligible homogeneous
carrier metadata and proven host transformation guards. The new node describes
the certified host result; the compiler's installation layer realizes it.

## No-change candidates and lifetime

An identity/no-change candidate is skipped without consuming successful-rewrite
budget. No-change comparison must distinguish opaque value identity from mere
textual resemblance. It is a driver optimization, not permission to coalesce
distinct source values.

The C host owns every constructed value, including declined/no-change candidates
and values allocated during a run that later fails. A per-run arena is a useful
ownership model. Constructors allocate compiler data; they do not evaluate the
source operands. Chapter 20 covers the callback and ownership boundary.

[Previous](14-effects-totality-and-value-identity.md) · [Contents](README.md) · [Next: HIR fixtures](16-hir-fixtures-and-provenance.md)
