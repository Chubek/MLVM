Kernel ExampleDiamond(inherits ExampleLeft, ExampleRight);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

LocalSubtractZero :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger { $1 `minus` $2 ~> $1; }
