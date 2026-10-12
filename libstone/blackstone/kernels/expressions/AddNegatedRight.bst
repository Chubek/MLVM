// Negation is represented by wrapping subtraction from concrete zero.
Kernel AddNegatedRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
AddNegationToSubtract :: Binary(Term!resolves, Binary(Term!exact(0), Term!resolves)) -> Term
where ModularInteger { $1 `plus` ($2 `minus` $3) ~> $1 `minus` $3; }
