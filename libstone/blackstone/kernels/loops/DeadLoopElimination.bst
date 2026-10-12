// Must prove finite termination and no observable body/exit effects, not just
// absence of live scalar results. Preserve bound evaluation and final values.
Kernel DeadLoopElimination;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::DeadLoopSafe;
DropUnobservableFiniteLoop :: Binary(Term!resolves, Term!resolves) -> Binary
where DeadLoopSafe { $1 `loop` $2 ~> $1 `loop_exit` $2; }
