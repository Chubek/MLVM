// Positive finite factor and cleanup plan are part of the domain descriptor.
// Preserve sequential dependence order, exceptional exits and overflow semantics.
Kernel LoopUnrolling;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::UnrollSafe;
UnrollCertifiedFactor :: Binary(Term!resolves, Term!resolves) -> Binary
where UnrollSafe { $1 `loop` $2 ~> $1 `unrolled_loop` $2; }
