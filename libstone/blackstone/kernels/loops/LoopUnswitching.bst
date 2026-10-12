// Invariant condition is stable and safe to evaluate outside the loop. The selected
// condition and clone plan are packaged in descriptors; repair all exit phis.
Kernel LoopUnswitching;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::UnswitchSafe;
UnswitchInvariantCondition :: Binary(Term!resolves, Term!resolves) -> Binary
where UnswitchSafe { $1 `loop` $2 ~> $1 `unswitched_loop` $2; }
