// Associativity requires identical scope, cleanup, exception and lifetime boundaries.
Kernel SequenceReassociation;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::SequenceAssociative;
RightAssociateStatements :: Binary(Binary(Term!resolves, Term!resolves), Term!resolves) -> Binary
where SequenceAssociative { ($1 `seq` $2) `seq` $3 ~> $1 `seq` ($2 `seq` $3); }
