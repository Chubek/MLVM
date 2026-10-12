# Chapter 22 — Structured-host kernels

The [catalog](../kernels/CATALOG.md) contains 85 kernels and 100 local rules:
the 16-rule acceptance kernel and 84 additional single-rule kernels. Beyond
expression identities, it provides contracts for statements, memory, control
flow, loops and polyhedral transformations.

## Descriptor carriers

The v0 syntax remains Term leaves and Binary-shaped patterns. Structured views
map opaque typed host descriptors to those sorts. A descriptor can encapsulate
a statement sequence, memory state, CFG region, loop plan or affine schedule.

| Directory | Carrier domain | Example transformations |
| --- | --- | --- |
| `statements` | `statement` | Sequencing, discard, scopes, assignments, cleanup |
| `memory` | `memory` | Forwarding, redundant loads/stores, copy/move specialization |
| `control-flow` | `control` | Branches, switches, blocks, phis, critical edges |
| `loops` | `loop` | Trip counts, invariants, fusion/fission, vectorization |
| `polyhedra` | `polyhedral` | Interchange, tiling, skewing, schedule fusion |

Carrier width/signedness fields are equality labels, not an instruction to perform
machine-width arithmetic on a CFG or affine set. Embedded schemas and exact
mathematical integers belong to the host payload representation.

## A zero-trip contract

The actual kernel is:

```bst
Kernel ZeroTripLoopElimination;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::ZeroTripSafe;

ElideZeroTripBody :: Binary(Term!resolves, Term!resolves) -> Binary
where ZeroTripSafe { $1 `loop` $2 ~> $1 `loop_exit` $2; }
```

`ZeroTripSafe` must prove zero iterations on every admitted path and justify the
complete transformation. `loop_exit` retains required bounds/initialization
evaluations, final induction values and exceptional behavior. It is a certified
result descriptor, not a bundled CFG mutation algorithm.

```sh
python3 -m blackstone apply kernels/loops/ZeroTripLoopElimination.bst \
  examples/loop-descriptor.json --trace
```

The reference adapter leaves this fixture unchanged. It knows the carrier shape
but has no loop-domain proof. Names like `bounds_descriptor` do not encode a
trip-count analysis, and an import alone does not certify zero iterations.

## Whole-transformation proofs

Catalog host rules must import and use a registered transformation-proof
capability for their domain. Runtime checks establish homogeneous carrier types,
resolvedness and eligible flags before a host rewrite. Cross-domain operator
trees are static errors; host operators never enter modular integer folding.

Zero-argument proof queries inspect the complete matched transformation and host
analysis context. Unary facts such as condition or empty-domain properties inspect
the selected capture. A proven query accounts for implicit dependencies, effects,
exceptions, lifetimes, aliasing and embedded typing.

The compiler supplies construction and atomic installation, then repairs relevant
def-use, CFG, phi, MemorySSA or dependence metadata. Polyhedral transformations
also need exact set/schedule semantics under their parameter context. Tests with
proof-providing adapters demonstrate gating and descriptor shape; they do not
establish that an external analysis solver's claims are true.

**Read next:** chapter 23 describes validating these proof boundaries, and chapter
24 explains the surrounding compiler pass architecture.

[Previous](21-native-ir-profiles-and-persistence.md) · [Contents](README.md) · [Next: Correctness checks](23-testing-and-correctness.md)
