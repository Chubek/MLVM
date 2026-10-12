// Exactly one iteration; preserve bounds evaluation, induction exits and EH edges.
Kernel SingleTripLoopElimination;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::SingleTripSafe;
InlineSingleIteration :: Binary(Term!resolves, Term!resolves) -> Binary
where SingleTripSafe { $1 `loop` $2 ~> $1 `inline_iteration` $2; }
