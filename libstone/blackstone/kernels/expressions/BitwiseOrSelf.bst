// Stable identity alone does not permit eliminating a repeated evaluation.
Kernel BitwiseOrSelf(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
OrIdempotence :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1) { $1 `bit_or` $2 ~> $1; }
