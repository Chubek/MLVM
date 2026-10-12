// OR identity preserves effects and exceptional behavior of the value.
Kernel OrZeroLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
OrLeftIdentity :: Binary(Term!exact(0), Term!resolves) -> Term
where ModularInteger { $1 `bit_or` $2 ~> $2; }
