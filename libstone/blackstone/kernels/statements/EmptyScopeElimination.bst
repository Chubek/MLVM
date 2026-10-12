// NoOp body is insufficient alone: implicit cleanup and labels must also be inert.
Kernel EmptyScopeElimination;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::{NoOp, ScopeElisionSafe};
DropEmptyScope :: Binary(Term!resolves, Term!resolves) -> Term
where NoOp($1) && ScopeElisionSafe { $1 `scope` $2 ~> $2; }
