// Order proof is true only for an inversion in a stable host canonical key.
// Independence includes memory, exceptions, synchronization and termination.
Kernel IndependentStatementOrdering;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::{IndependentStatements, CanonicalStatementOrder};
SortIndependentStatements :: Binary(Term!resolves, Term!resolves) -> Binary
where IndependentStatements && CanonicalStatementOrder { $1 `seq` $2 ~> $2 `seq` $1; }
