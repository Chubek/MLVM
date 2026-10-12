// Inherited folding reduces the newly exposed literal/literal subtree.
Kernel OrAssociativeConstants(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
GroupOrLiterals :: Binary(Binary(Term!nonliteral, Term!literal), Term!literal) -> Term
where ModularInteger { ($1 `bit_or` $2) `bit_or` $3 ~> $1 `bit_or` ($2 `bit_or` $3); }
