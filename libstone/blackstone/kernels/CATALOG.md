# Blackstone kernel catalog

**85 `.bst` kernels, 100 local rules**: the original 16-rule
`AlgebraicSimplification.bst` and 84 additional single-rule kernels. Every file
parses through the native dparser frontend and validates against explicit
operator, view, capability, capture, and inheritance registries.

```sh
# From libstone/blackstone, after building the native parser:
PYTHONPATH=python python3 -m blackstone check kernels
PYTHONPATH=python python3 -m blackstone apply \
  kernels/expressions/XorZeroRight.bst input.json --trace
```

`check` on a directory recursively validates its `.bst` files as a catalog.
`--kernel-dir` also searches recursively for named inheritance dependencies.

## Inventory

| Directory | Added kernels | Coverage |
| --- | ---: | --- |
| `expressions/` | 32 | Bitwise identities, absorption/cancellation, canonicalization, wrapping arithmetic, factoring, division |
| `statements/` | 12 | Sequencing, evaluation discard, ordering, assignments, declarations, scopes, unreachable suffixes, assertions, cleanup, copies |
| `memory/` | 8 | Forwarding, redundant loads/stores, copy/move specialization, fills, narrow loads |
| `control-flow/` | 12 | Branches, switches, CFG blocks/edges, threading, if-conversion, returns, phis |
| `loops/` | 12 | Trip counts, invariants, unswitching, peeling/unrolling, fusion/fission, reductions, vectorization, deletion |
| `polyhedra/` | 8 | Interchange, tiling, skewing, schedule fusion/distribution, parallelization, domain coalescing, emptiness |

### Expressions

`AndAllOnesRight`, `AndAllOnesLeft`, `OrAllOnesRight`, `OrAllOnesLeft`,
`XorZeroRight`, `XorZeroLeft`, `AndZeroLeft`, `OrZeroLeft`, `BitwiseOrSelf`,
`DoubleXorCancellationRight`, `DoubleXorCancellationLeft`, `AndAbsorption`,
`OrAbsorption`, `AndAssociativeConstants`, `OrAssociativeConstants`,
`XorAssociativeConstants`, `AndConstantRight`, `OrConstantRight`,
`XorConstantRight`, `AddRepeatedOperand`, `DoubleNegation`, `SubtractSumRight`,
`SubtractDifferenceRight`, `AddDifferenceRight`, `AddNegatedRight`,
`SubtractNegatedRight`, `FactorCommonLeft`, `FactorCommonRight`,
`MultiplyMinusOne`, `DivideSelf`, `ZeroDividend`, `CancelAddSubtract`.

These kernels inherit `ConstantFolding`. `AllOnes($n)` is true only for a resolved
literal equal to the exact type's all-ones representation. `Nonzero($n)` is known
for concrete integer literals; nonliteral facts require host analysis. Queries
never evaluate source programs. Every rule requires `ModularInteger` over the
whole exposed skeleton. Removed/moved evaluations carry explicit purity/totality
guards. Unrepresentable replacements fail construction, including positive one
in signed one-bit types and positive two in signed two-bit types.

### Statements

`SequenceEmptyLeft`, `SequenceEmptyRight`, `SequenceReassociation`,
`DiscardPureExpression`, `IndependentStatementOrdering`,
`DeadAssignmentElimination`, `UnusedDeclarationElimination`,
`EmptyScopeElimination`, `UnreachableStatementSuffix`,
`DuplicateAssertionElimination`, `RedundantCleanupElimination`,
`StatementCopyPropagation`.

### Memory

`StoreLoadForwarding`, `RedundantLoadReuse`, `DeadStoreElimination`,
`StoreOfLoadedValue`, `ZeroLengthMemoryCopy`, `NonOverlappingMoveToCopy`,
`DuplicateMemoryFill`, `NarrowLoadFolding`.

### Control flow

`ConstantTrueBranch`, `ConstantFalseBranch`, `EqualSuccessorBranch`,
`EmptySwitch`, `SingleCaseSwitch`, `UnreachableBlockElimination`,
`LinearBlockMerge`, `JumpThreading`, `DiamondIfConversion`, `ReturnTailMerge`,
`CriticalEdgeSplitting`, `UniformPhiElimination`.

### Loops

`ZeroTripLoopElimination`, `SingleTripLoopElimination`, `LoopInvariantHoisting`,
`LoopInvariantSinking`, `LoopUnswitching`, `LoopPeeling`, `LoopUnrolling`,
`LoopFusion`, `LoopFission`, `ReductionPrivatization`, `LoopVectorization`,
`DeadLoopElimination`.

### Polyhedra

`PolyhedralInterchange`, `PolyhedralTiling`, `PolyhedralSkewing`,
`PolyhedralScheduleFusion`, `PolyhedralDistribution`,
`PolyhedralParallelization`, `PolyhedralDomainCoalescing`,
`PolyhedralEmptyDomain`.

## Structured-host contracts

The v0 syntax has `Term` leaves and binary-shaped patterns. Extended views map
structured host descriptors into those same sorts:

| Host views (Term, Binary respectively) | Carrier domain |
| --- | --- |
| `HIR::Statements::{Statement, Composition}` | `statement` |
| `HIR::Memory::{State, Operation}` | `memory` |
| `HIR::ControlFlow::{Region, Operation}` | `control` |
| `HIR::Loops::{Descriptor, Operation}` | `loop` |
| `HIR::Polyhedra::{Descriptor, Operation}` | `polyhedral` |

A descriptor encapsulates a host object or plan: operands, block edges, memory
state, iteration domain, schedule, etc. All exposed nodes within one rule share
a carrier `Type`. Embedded host value types/payload schemas are checked by host
proofs. Carrier width/signedness fields are type-equality labels, not machine
integer semantics for affine sets or statements. Exact affine integers and
parameter contexts belong to the host payload representation.

`python/blackstone/catalog.py` registers the operator/capability names. Each host
rule must import and use a transformation-proof capability for its domain.
Cross-domain operator trees are rejected statically. Runtime carrier,
resolvedness and flag checks precede any host rewrite. Host operators never
satisfy `ModularInteger` or enter integer constant folding.

The reference `TreeAdapter` returns **unknown** for host proofs. Imports declare
required contracts; they do not certify transformations. A compiler adapter must:

1. Map host objects/plans to stable typed descriptor identities and supply their
   payload schemas/operator meanings.
2. Implement `predicate` queries for `Optimization::<module>::<proof>`.
   Zero-argument proofs inspect the whole matched transformation and analysis
   context; unary facts inspect the named capture.
3. Certify implicit effects, exceptions, divergence, poison/undef, aliases,
   lifetimes, dependencies and exact embedded result types.
4. Implement construction and atomic installation. `unrolled_loop`, `tiled_scop`,
   `split_edge` and similar operators are certified result/installation
   descriptors whose realization belongs to the host.
5. Repair def-use/CFG/phi/MemorySSA/dependence metadata, invalidate affected
   analyses, and apply the rewrite budget across host requeues.

The catalog supplies executable matching, proof gating and descriptor rewriting.
It does not bundle a CFG mutator, vector backend or affine dependence solver.
Proof-providing test adapters exercise construction/gating, not the truth of
external host proofs.

### Proof obligations

Every source file states its particular obligations. Common requirements:

* Statement reassociation preserves scope/lifetime/cleanup boundaries.
  `CanonicalStatementOrder` holds only for an inversion in a stable total order;
  the swapped result must cease to qualify.
* Memory proofs include full extent/type/alignment, pointer provenance, clobbers,
  readers, volatile/atomic observations, traps and argument-evaluation effects.
  Equal addresses alone are insufficient.
* CFG proofs include normal, exceptional, indirect and deoptimization edges,
  dominance and compatible phi substitutions. Folding a condition does not
  automatically allow discarding its evaluation.
* Loop plans supply finite factors/counts, chosen transformations and exit
  repairs. Zero-trip speculation, early exits, overflow and loop-carried
  dependences are covered. Descriptor duplication does not duplicate bounds
  evaluation.
* Polyhedral proofs use exact integer sets/maps under explicit parameters and
  preserve statement identity/multiplicity and legal dependence order. Tiling
  covers partial tiles exactly; skewing preserves the integer lattice;
  coalescing requires set equality, not convex overapproximation; parallelization
  certifies races/reductions/synchronization and exceptional behavior.

## Tests

`ctest --test-dir build --output-on-failure` runs the original contract and
`tests/test_catalog.py`. Catalog tests enforce the exact 84-file distribution,
unique kernel/local-rule names, nonempty rules, native parsing/static checking
of every file, exhaustive small-width expression equivalence, proof-by-proof
false/unknown rejection, proven-host construction/idempotence, domain isolation,
and whole-catalog CLI validation.
