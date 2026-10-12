// Group adjacent AND constants without moving nonliteral evaluations.
Kernel AndAssociativeConstants(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
GroupAndLiterals :: Binary(Binary(Term!nonliteral, Term!literal), Term!literal) -> Term
where ModularInteger { ($1 `bit_and` $2) `bit_and` $3 ~> $1 `bit_and` ($2 `bit_and` $3); }
