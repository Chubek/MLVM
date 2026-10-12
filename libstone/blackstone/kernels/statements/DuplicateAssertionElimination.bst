// Same assertion descriptor; proof excludes changing state, diagnostics and effects.
Kernel DuplicateAssertionElimination;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::DuplicateAssertSafe;
DropRepeatedAssertion :: Binary(Term!resolves, Term!same($1)) -> Term
where DuplicateAssertSafe { $1 `seq` $2 ~> $1; }
