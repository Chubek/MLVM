// XOR reassociation is oriented toward a right-side constant group.
Kernel XorAssociativeConstants(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
GroupXorLiterals :: Binary(Binary(Term!nonliteral, Term!literal), Term!literal) -> Term
where ModularInteger { ($1 `bit_xor` $2) `bit_xor` $3 ~> $1 `bit_xor` ($2 `bit_xor` $3); }
