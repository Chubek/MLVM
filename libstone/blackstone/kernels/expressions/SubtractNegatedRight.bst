// Removes an arithmetic node without removing the value's evaluation.
Kernel SubtractNegatedRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
SubtractNegationToAdd :: Binary(Term!resolves, Binary(Term!exact(0), Term!resolves)) -> Term
where ModularInteger { $1 `minus` ($2 `minus` $3) ~> $1 `plus` $3; }
