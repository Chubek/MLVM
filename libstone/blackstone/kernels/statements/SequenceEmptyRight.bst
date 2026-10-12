// Preserve exceptional exits and debug/lifetime obligations of the prefix.
Kernel SequenceEmptyRight;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::{NoOp, SequenceIdentitySafe};
RemoveEmptySuffix :: Binary(Term!resolves, Term!resolves) -> Term
where NoOp($2) && SequenceIdentitySafe { $1 `seq` $2 ~> $1; }
