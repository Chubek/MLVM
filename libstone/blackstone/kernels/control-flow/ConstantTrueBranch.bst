// jump_true retains the condition evaluation and selects the true edge bundle.
// Proof updates successor/phi, exceptional and deoptimization state consistently.
Kernel ConstantTrueBranch;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::{ConditionTrue, BranchFoldSafe};
SelectTrueSuccessor :: Binary(Term!resolves, Term!resolves) -> Binary
where ConditionTrue($1) && BranchFoldSafe { $1 `branch` $2 ~> $1 `jump_true` $2; }
