// Exact emptiness for every admitted parameter valuation; retain parameter/bound
// evaluation, runtime guards, live-outs and required exceptions in the exit plan.
Kernel PolyhedralEmptyDomain;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::{EmptyDomain, EmptyScopSafe};
ElideEmptyAffineRegion :: Binary(Term!resolves, Term!resolves) -> Binary
where EmptyDomain($1) && EmptyScopSafe { $1 `scop` $2 ~> $1 `empty_scop` $2; }
