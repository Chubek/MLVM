// Both the repeated x and discarded y must be pure and total.
Kernel AndAbsorption(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
AbsorbDisjunction :: Binary(Term!resolves, Binary(Term!same($1), Term!resolves)) -> Term
where ModularInteger && Pure($1) && Total($1) && Pure($3) && Total($3)
{ $1 `bit_and` ($2 `bit_or` $3) ~> $1; }
