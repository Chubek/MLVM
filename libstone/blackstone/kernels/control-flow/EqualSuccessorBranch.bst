// Equal targets are stable block identities, including compatible incoming phi uses.
// The condition can disappear only if its evaluation has no observable behavior.
Kernel EqualSuccessorBranch;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires HIR::Effects::{Pure, Total};
requires Optimization::ControlFlow::BranchConditionDiscardSafe;
CollapseEqualTargets :: Binary(Term!resolves, Binary(Term!resolves, Term!same($2))) -> Term
where Pure($1) && Total($1) && BranchConditionDiscardSafe
{ $1 `branch` ($2 `targets` $3) ~> $2; }
