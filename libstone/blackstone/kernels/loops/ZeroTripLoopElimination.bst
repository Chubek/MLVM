// Domain proves zero iterations on all admitted paths. loop_exit retains bounds,
// initialization, final induction values and required exceptions/evaluations.
Kernel ZeroTripLoopElimination;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::ZeroTripSafe;
ElideZeroTripBody :: Binary(Term!resolves, Term!resolves) -> Binary
where ZeroTripSafe { $1 `loop` $2 ~> $1 `loop_exit` $2; }
