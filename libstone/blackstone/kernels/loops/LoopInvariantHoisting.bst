// Invariant descriptor selection requires dominance, no clobber/alias, speculation
// safety on zero-trip paths, lifetime validity and preserved exceptional ordering.
Kernel LoopInvariantHoisting;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::InvariantHoistSafe;
HoistCertifiedInvariants :: Binary(Term!resolves, Term!resolves) -> Binary
where InvariantHoistSafe { $1 `loop` $2 ~> $1 `hoisted_loop` $2; }
