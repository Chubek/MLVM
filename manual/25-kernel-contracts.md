# 25. Kernel contracts, proofs, and validation

[Contents](README.md) · [Next: Host optimizers](26-host-optimizers.md)

Validation has three boundaries:

1. Blackstone's generated DParser frontend checks `.bst` syntax and source spans.
2. `bs_profile_compile` resolves concerns, operators, capabilities and inheritance,
   and validates capture numbering, signature/match trees, construction and guards.
3. The typed adapter supplies concrete value semantics and tri-state facts for
   each candidate. Only `BS_TRUE` permits a guard to succeed.

MLVM rejects embedded NUL and files larger than 16 MiB at the source boundary.
Native rule trees and runtime traversal are bounded by Blackstone's limits.
Unknown symbols, unsupported sorts/refinements, forward identity captures,
duplicate rules and inheritance cycles produce diagnostics.

## Integer execution domain

The built-in adapter implements exact-width modular addition, subtraction,
multiplication and bitwise AND/OR/XOR, plus signed or unsigned division according
to the representation. Division by zero and signed minimum/-1 are not folded.
MLVM iN arithmetic uses signed literal interpretation; unsigned bit-pattern
constant spelling is normalized to that interpretation. `arith.divui` is kept
opaque by the signed MLVM adapter. Firestone dataflow explicitly records its
signedness, and its Boolean i1 is excluded from modular-integer matching.

`ModularInteger` checks every exposed binary node and immediate operand for
homogeneous type, registered semantics and absent semantic flags. MLVM operations
with extra attributes other than `value` and `origin` do not qualify. New
arithmetic is constructed at the original type; capture forwarding preserves
identity. Unsupported domains never become integer proofs.

`Associative` and `Commutative` are proved for unflagged modular addition and
multiplication. `Pure` and `Total` are conservative facts. `AllOnes` and `Nonzero`
are available for representable concrete literals. Host-descriptor predicates
remain unknown in the generic integer adapter.

## Scheduling and convergence

The native driver visits children first, applies the first matching rule in
resolved order, then revisits replacements to a fixed point. Each kernel action
has a budget across all rewritten roots/operations. Exhaustion fails without
publishing partial IR or trace; the diagnostic identifies recent rules.

Integration tests exercise all 16 algebra rules, exhaustive small signed widths,
unknown facts, flags, distinct identities, exceptional division, idempotence,
budget rollback and binary-profile round trips. Catalog compilation validates
declarations; it is not a proof of arbitrary user rewrite equivalence.
