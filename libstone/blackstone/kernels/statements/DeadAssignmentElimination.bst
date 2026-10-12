// assign carries assignment and continuation descriptors; proof covers value effects,
// escape/liveness, volatile/atomic state, exceptions, and observable destruction.
Kernel DeadAssignmentElimination;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::DeadAssignmentSafe;
DropDeadAssignment :: Binary(Term!resolves, Term!resolves) -> Term
where DeadAssignmentSafe { $1 `assign` $2 ~> $2; }
