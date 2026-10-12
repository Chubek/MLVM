// Reduction identity/operator and final combine are proven valid; no observable
// intermediate accumulator use, aliasing, overflow change or floating reassociation.
Kernel ReductionPrivatization;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::ReductionPrivatizationSafe;
PrivatizeCertifiedReduction :: Binary(Term!resolves, Term!resolves) -> Binary
where ReductionPrivatizationSafe { $1 `loop` $2 ~> $1 `privatized_reduction` $2; }
