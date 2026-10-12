// Moving/removing evaluations of the common factor needs purity and totality.
Kernel FactorCommonRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
ExtractRightFactor :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!resolves, Term!same($2))) -> Term
where ModularInteger && Pure($2) && Total($2)
{ ($1 `times` $2) `plus` ($3 `times` $4) ~> ($1 `plus` $3) `times` $2; }
