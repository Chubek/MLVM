// Reassociation keeps every original operand evaluation in order.
Kernel AddDifferenceRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
FlattenAddedDifference :: Binary(Term!resolves, Binary(Term!resolves, Term!resolves)) -> Term
where ModularInteger { $1 `plus` ($2 `minus` $3) ~> ($1 `plus` $2) `minus` $3; }
