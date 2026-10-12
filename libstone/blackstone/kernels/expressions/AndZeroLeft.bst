// AND annihilation may discard only a pure, total evaluation.
Kernel AndZeroLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};
AndLeftAnnihilator :: Binary(Term!exact(0), Term!resolves) -> Term
where ModularInteger && Pure($2) && Total($2) { $1 `bit_and` $2 ~> $1; }
