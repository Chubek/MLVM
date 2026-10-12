// Host proves cleanup already discharged and no exception/lifetime action remains.
Kernel RedundantCleanupElimination;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::RedundantCleanupSafe;
DropDischargedCleanup :: Binary(Term!resolves, Term!resolves) -> Term
where RedundantCleanupSafe { $1 `cleanup` $2 ~> $2; }
