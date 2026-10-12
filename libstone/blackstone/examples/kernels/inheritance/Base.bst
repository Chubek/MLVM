Kernel ExampleBase(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

BaseAddZeroRight :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger { $1 `plus` $2 ~> $1; }
