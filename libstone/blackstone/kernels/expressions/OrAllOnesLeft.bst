// Do not erase an effectful, trapping, or undefined right operand.
Kernel OrAllOnesLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::AllOnes;
requires HIR::Effects::{Pure, Total};
KeepOnesLeft :: Binary(Term!literal, Term!resolves) -> Term
where ModularInteger && AllOnes($1) && Pure($2) && Total($2)
{ $1 `bit_or` $2 ~> $1; }
