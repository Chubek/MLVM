// exact(-1) excludes unsigned types; negation still wraps in signed types.
Kernel MultiplyMinusOne(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
MultiplyNegativeUnit :: Binary(Term!resolves, Term!exact(-1)) -> Term
where ModularInteger { $1 `times` $2 ~> 0 `minus` $1; }
