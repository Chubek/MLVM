// Wrapping arithmetic permits this orientation while retaining x,y,z order.
Kernel SubtractSumRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
FlattenSubtractedSum :: Binary(Term!resolves, Binary(Term!resolves, Term!resolves)) -> Term
where ModularInteger { $1 `minus` ($2 `plus` $3) ~> ($1 `minus` $2) `minus` $3; }
