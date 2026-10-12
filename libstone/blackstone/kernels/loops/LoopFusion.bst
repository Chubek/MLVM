// Compatible iteration spaces; no backward cross-loop dependences, changed effect
// order, bound evaluation, zero-trip, exception or lifetime behavior.
Kernel LoopFusion;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::LoopFusionSafe;
FuseAdjacentLoops :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!resolves, Term!resolves)) -> Binary
where LoopFusionSafe
{ ($1 `loop` $2) `loop_pair` ($3 `loop` $4) ~> ($1 `paired_domain` $3) `fused_loop` ($2 `seq_loop_body` $4); }
