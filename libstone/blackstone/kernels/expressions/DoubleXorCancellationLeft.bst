// Cancellation preserves the middle operand's single evaluation.
Kernel DoubleXorCancellationLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
CancelRepeatedLeft :: Binary(Binary(Term!resolves, Term!resolves), Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1)
{ ($1 `bit_xor` $2) `bit_xor` $3 ~> $2; }
