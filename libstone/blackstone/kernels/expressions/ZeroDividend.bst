// Preserve division-by-zero behavior by requiring a proven nonzero divisor.
Kernel ZeroDividend(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Arithmetic::Values::Nonzero;
requires HIR::Effects::{Pure, Total};
ZeroQuotient :: Binary(Term!exact(0), Term!resolves) -> Term
where ModularInteger && Nonzero($2) && Pure($2) && Total($2)
{ $1 `divide` $2 ~> $1; }
