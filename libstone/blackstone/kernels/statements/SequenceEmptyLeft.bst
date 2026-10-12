// NoOp is an effect-free, normally terminating statement; retain successor metadata.
Kernel SequenceEmptyLeft;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::{NoOp, SequenceIdentitySafe};
RemoveEmptyPrefix :: Binary(Term!resolves, Term!resolves) -> Term
where NoOp($1) && SequenceIdentitySafe { $1 `seq` $2 ~> $2; }
