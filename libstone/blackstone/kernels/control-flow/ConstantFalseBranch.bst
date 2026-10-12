// False proof is valid on every incoming path; condition evaluation is retained.
Kernel ConstantFalseBranch;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::{ConditionFalse, BranchFoldSafe};
SelectFalseSuccessor :: Binary(Term!resolves, Term!resolves) -> Binary
where ConditionFalse($1) && BranchFoldSafe { $1 `branch` $2 ~> $1 `jump_false` $2; }
