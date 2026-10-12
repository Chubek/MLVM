// Declaration removal requires no observable initialization or lifetime actions.
Kernel UnusedDeclarationElimination;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::UnusedDeclarationSafe;
DropUnusedDeclaration :: Binary(Term!resolves, Term!resolves) -> Term
where UnusedDeclarationSafe { $1 `declare` $2 ~> $2; }
