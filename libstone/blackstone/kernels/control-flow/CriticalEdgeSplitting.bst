// Proof identifies an unsplit critical edge; installation creates one fresh block
// and redirects precisely this incoming edge's phi/EH/profile/debug metadata.
Kernel CriticalEdgeSplitting;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::CriticalEdgeSafe;
SplitCriticalControlEdge :: Binary(Term!resolves, Term!resolves) -> Binary
where CriticalEdgeSafe { $1 `edge` $2 ~> $1 `split_edge` $2; }
