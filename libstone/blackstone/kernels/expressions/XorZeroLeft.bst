// The left zero is concrete, not merely constant-foldable.
Kernel XorZeroLeft(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
XorLeftIdentity :: Binary(Term!exact(0), Term!resolves) -> Term
where ModularInteger { $1 `bit_xor` $2 ~> $2; }
