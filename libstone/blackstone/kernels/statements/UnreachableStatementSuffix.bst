// A suffix has no independent label, exceptional entry, or observable registration.
Kernel UnreachableStatementSuffix;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::{NoNormalExit, UnreachableSuffixSafe};
DropUnreachableSuffix :: Binary(Term!resolves, Term!resolves) -> Term
where NoNormalExit($1) && UnreachableSuffixSafe { $1 `seq` $2 ~> $1; }
