// Positive tile sizes in the certified plan. Exact floor/remainder bounds cover
// partial tiles once, preserve dependences, and cannot introduce machine overflow.
Kernel PolyhedralTiling;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::TilingSafe;
TileLegalAffineSchedule :: Binary(Term!resolves, Term!resolves) -> Binary
where TilingSafe { $1 `scop` $2 ~> $1 `tiled_scop` $2; }
