// A width-specific all-ones literal is the bitwise AND identity.
Kernel AndAllOnesRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::AllOnes;
KeepValue :: Binary(Term!resolves, Term!literal) -> Term
where ModularInteger && AllOnes($2) { $1 `bit_and` $2 ~> $1; }
