// Literal-left/nonliteral-right prevents swap/swap-back oscillation.
Kernel AndConstantRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
NormalizeAndConstant :: Binary(Term!literal, Term!nonliteral) -> Term
where ModularInteger { $1 `bit_and` $2 ~> $2 `bit_and` $1; }
