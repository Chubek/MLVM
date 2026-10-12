# Chapter 1 — What Blackstone does

Blackstone is a declarative language for specifying guarded compiler rewrites.
A `.bst` file states which HIR shape to recognize, which semantic facts must be
proven, and which value or expression replaces the match. The host compiler
supplies typed values, stable identity, operator semantics and analysis facts.

For example, an integer identity says that `x plus 0` can become `x` when the
explicitly matched addition has homogeneous, wrapping integer semantics. Another
rule says that `x times 0` can become zero only when discarding `x` also preserves
effects and exceptional behavior. These have similar algebraic appearances and
different proof obligations.

## The implemented pipeline

```text
.bst source
    -> native dparser frontend
    -> source-spanned AST
    -> declaration resolution and rule validation
    -> typed HIR matching and guard queries
    -> immutable replacement construction
    -> deterministic fixed-point rewriting
```

The Python driver operates on the reference HIR adapter. The native C driver
compiles frontend IR into an immutable profile and performs matching and
scheduling through host callbacks. `--engine python` and `--engine native` select
these drivers; both use the same source language and reference integer contracts.
The native bridge uses Python's arbitrary-width integer implementation for its
reference host, while C integrations provide their own integer operations.

## The first acceptance kernel

[`AlgebraicSimplification.bst`](../kernels/expressions/AlgebraicSimplification.bst)
contains 16 local rules, including zero/one identities, guarded annihilators,
self-cancellation, constant movement and reassociation. Its inherited
`ConstantFolding` entry is additional to those 16 rules.

The original example follows this sequence:

```text
((x plus 3) plus 4)
    -> (x plus (3 plus 4))       ReassociateAddConstants
    -> (x plus 7)                ConstantFolding
```

The example is signed 8-bit. A narrower type may produce a wrapped constant and
trigger another local identity rule. Exact type preservation is part of the
rewrite contract, so the driver cannot silently widen the intermediate sum.

## Scope of v0

The language has binary-shaped signatures, refined Term leaves, backtick infix
operators, conjunction guards and one directed replacement per rule. It includes
explicit registries and a catalog of expression and structured-host contracts.
Tree fixtures are runnable; structured-host descriptors require the compiler's
transformation proofs and installation machinery.

Validation establishes well-formed declarations, references and contracts. Rule
authors still establish equivalence for arbitrary new rules. Matching and guards
inspect metadata; they never execute the source program to discover a result.

**Try next:** build the tools in chapter 2, then run the first complete rule in
chapter 3. The [example collection](../examples/README.md) also contains deliberate
nonmatches, which are useful demonstrations of the contracts working correctly.

[Contents](README.md) · [Next: Building and running the tools](02-build-and-tooling.md)
