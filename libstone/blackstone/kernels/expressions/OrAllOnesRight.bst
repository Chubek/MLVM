// Removing the left evaluation requires purity and totality.
Kernel OrAllOnesRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::AllOnes;
requires HIR::Effects::{Pure, Total};
KeepOnesRight :: Binary(Term!resolves, Term!literal) -> Term
where ModularInteger && AllOnes($2) && Pure($1) && Total($1)
{ $1 `bit_or` $2 ~> $2; }
