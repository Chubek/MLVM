// The identical addend/subtrahend is erased only with evaluation proofs.
Kernel CancelAddSubtract(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
CancelAddedValue :: Binary(Binary(Term!resolves, Term!resolves), Term!same($2)) -> Term
where ModularInteger && Pure($2) && Total($2)
{ ($1 `plus` $2) `minus` $3 ~> $1; }
