// Nonzero is a proof, not an assumption; signed 1-bit positive one cannot build.
Kernel DivideSelf(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::Nonzero;
requires HIR::Effects::{Pure, Total};
DivideIdenticalValues :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Nonzero($1) && Pure($1) && Total($1)
{ $1 `divide` $2 ~> 1; }
