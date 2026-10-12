// Excludes checked, saturating and overflow-poison subtraction.
Kernel SubtractDifferenceRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
FlattenSubtractedDifference :: Binary(Term!resolves, Binary(Term!resolves, Term!resolves)) -> Term
where ModularInteger { $1 `minus` ($2 `minus` $3) ~> ($1 `minus` $2) `plus` $3; }
