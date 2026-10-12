// Certified integral invertible schedule transform preserves the iteration lattice,
// dependence order, exact inverse bounds and parameter assumptions.
Kernel PolyhedralSkewing;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::SkewingSafe;
SkewLegalAffineSchedule :: Binary(Term!resolves, Term!resolves) -> Binary
where SkewingSafe { $1 `scop` $2 ~> $1 `skewed_scop` $2; }
