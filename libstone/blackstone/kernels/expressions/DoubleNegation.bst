// Modular double negation is valid even at the signed minimum.
Kernel DoubleNegation(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
CancelNegations :: Binary(Term!exact(0), Binary(Term!exact(0), Term!resolves)) -> Term
where ModularInteger { $1 `minus` ($2 `minus` $3) ~> $3; }
