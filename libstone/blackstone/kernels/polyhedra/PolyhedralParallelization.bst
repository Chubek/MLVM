// Selected band carries no dependence except explicitly privatized/legal reductions.
// Proof covers races, synchronization, finite termination, exceptions and numerics.
Kernel PolyhedralParallelization;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::ParallelScheduleSafe;
ParallelizeIndependentBand :: Binary(Term!resolves, Term!resolves) -> Binary
where ParallelScheduleSafe { $1 `scop` $2 ~> $1 `parallel_scop` $2; }
