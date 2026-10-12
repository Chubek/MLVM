// No forbidden dependence across partitions; duplicated domain descriptors are
// inert plans, not duplicated bound evaluations. Preserve exits and side effects.
Kernel LoopFission;
concerns HIR::Loops::Descriptor;
concerns HIR::Loops::Operation;
requires Optimization::Loops::LoopFissionSafe;
DistributeLoopPartitions :: Binary(Term!resolves, Binary(Term!resolves, Term!resolves)) -> Binary
where LoopFissionSafe
{ $1 `loop` ($2 `seq_loop_body` $3) ~> ($1 `loop` $2) `loop_pair` ($1 `loop` $3); }
