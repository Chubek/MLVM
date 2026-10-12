// Moving a literal does not reorder any nonliteral evaluations.
Kernel OrConstantRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
NormalizeOrConstant :: Binary(Term!literal, Term!nonliteral) -> Term
where ModularInteger { $1 `bit_or` $2 ~> $2 `bit_or` $1; }
