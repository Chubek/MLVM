// Exact integer affine domain/dependence relations under explicit parameters.
// Chosen dimension permutation preserves every dependence lexicographically;
// codegen bounds, overflow, side effects and exceptional paths remain equivalent.
Kernel PolyhedralInterchange;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::InterchangeSafe;
InterchangeLegalSchedule :: Binary(Term!resolves, Term!resolves) -> Binary
where InterchangeSafe { $1 `scop` $2 ~> $1 `interchanged_scop` $2; }
