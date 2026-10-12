// Strength reduction removes a repeated evaluation; 2 must be representable.
Kernel AddRepeatedOperand(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
DoubleToMultiply :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1) { $1 `plus` $2 ~> $1 `times` 2; }
