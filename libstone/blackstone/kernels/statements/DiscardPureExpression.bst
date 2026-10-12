// discard(value, continuation) evaluates the expression before its continuation.
Kernel DiscardPureExpression;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires HIR::Effects::{Pure, Total};
requires Optimization::Statements::StatementDiscardSafe;
ElideUnusedEvaluation :: Binary(Term!resolves, Term!resolves) -> Term
where Pure($1) && Total($1) && StatementDiscardSafe { $1 `discard` $2 ~> $2; }
