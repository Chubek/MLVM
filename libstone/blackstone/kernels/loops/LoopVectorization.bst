// Host plan certifies lane dependence, aliases/alignment, target operations,
// poison/trap/speculation behavior, reductions, tail handling and scalar exits.
Kernel LoopVectorization;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::VectorizationSafe;
VectorizeCertifiedLoop :: Binary(Term!resolves, Term!resolves) -> Binary
where VectorizationSafe { $1 `loop` $2 ~> $1 `vector_loop` $2; }
