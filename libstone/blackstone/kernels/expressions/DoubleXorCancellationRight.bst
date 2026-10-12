// Cancel both occurrences of the same pure, total right operand.
Kernel DoubleXorCancellationRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
CancelRepeatedRight :: Binary(Binary(Term!resolves, Term!resolves), Term!same($2)) -> Term
where ModularInteger && Pure($2) && Total($2)
{ ($1 `bit_xor` $2) `bit_xor` $3 ~> $1; }
