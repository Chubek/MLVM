// A folded constant is canonical on the right of XOR.
Kernel XorConstantRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
NormalizeXorConstant :: Binary(Term!literal, Term!nonliteral) -> Term
where ModularInteger { $1 `bit_xor` $2 ~> $2 `bit_xor` $1; }
