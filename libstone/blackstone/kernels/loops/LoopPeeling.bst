// Finite peel count is supplied in the domain plan; guards handle short trips,
// overflow, induction updates, exceptions, break/continue and residual exits.
Kernel LoopPeeling;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::PeelSafe;
PeelPlannedIterations :: Binary(Term!resolves, Term!resolves) -> Binary
where PeelSafe { $1 `loop` $2 ~> $1 `peeled_loop` $2; }
