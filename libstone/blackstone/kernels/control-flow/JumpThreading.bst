// Path facts prove a successor through an intermediate branch. Host preserves
// intermediate effects and repairs cloned state, dominance, phis and EH edges.
Kernel JumpThreading;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::JumpThreadingSafe;
ThreadProvenBranchPath :: Binary(Term!resolves, Term!resolves) -> Binary
where JumpThreadingSafe { $1 `branch` $2 ~> $1 `threaded_branch` $2; }
