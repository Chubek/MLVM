// Unique connecting edge, compatible scopes/EH state, and valid phi substitution.
// No address-taken block or required boundary may be removed.
Kernel LinearBlockMerge;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::LinearMergeSafe;
MergeLinearSuccessor :: Binary(Term!resolves, Term!resolves) -> Binary
where LinearMergeSafe { $1 `blocks` $2 ~> $1 `merged_block` $2; }
