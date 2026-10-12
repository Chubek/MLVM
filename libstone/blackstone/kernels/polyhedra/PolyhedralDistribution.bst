// Partition plan covers each original statement instance exactly once and respects
// every dependence across partitions, live-out, alias and observable ordering.
Kernel PolyhedralDistribution;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::DistributionSafe;
DistributeCertifiedScop :: Binary(Term!resolves, Term!resolves) -> Binary
where DistributionSafe { $1 `scop` $2 ~> $1 `distributed_scop` $2; }
