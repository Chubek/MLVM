// Dual absorption; identity-distinct x values must not match.
Kernel OrAbsorption(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
AbsorbConjunction :: Binary(Term!resolves, Binary(Term!same($1), Term!resolves)) -> Term
where ModularInteger && Pure($1) && Total($1) && Pure($3) && Total($3)
{ $1 `bit_or` ($2 `bit_and` $3) ~> $1; }
