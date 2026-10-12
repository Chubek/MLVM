// Works for both unsigned all-ones and signed negative one.
Kernel AndAllOnesLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::AllOnes;
KeepRightValue :: Binary(Term!literal, Term!resolves) -> Term
where ModularInteger && AllOnes($1) { $1 `bit_and` $2 ~> $2; }
