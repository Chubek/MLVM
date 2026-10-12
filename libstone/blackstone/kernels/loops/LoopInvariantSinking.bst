// Sinking preserves all exit uses and effects on early/exceptional exits, including
// zero-trip handling; host repairs LCSSA, dominance and loop-carried dependencies.
Kernel LoopInvariantSinking;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::InvariantSinkSafe;
SinkCertifiedInvariants :: Binary(Term!resolves, Term!resolves) -> Binary
where InvariantSinkSafe { $1 `loop` $2 ~> $1 `sunk_loop` $2; }
