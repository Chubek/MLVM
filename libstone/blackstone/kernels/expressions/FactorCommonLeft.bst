// The common factor is evaluated once rather than twice, so require both proofs.
Kernel FactorCommonLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
ExtractLeftFactor :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!same($1), Term!resolves)) -> Term
where ModularInteger && Pure($1) && Total($1)
{ ($1 `times` $2) `plus` ($3 `times` $4) ~> $1 `times` ($2 `plus` $4); }
