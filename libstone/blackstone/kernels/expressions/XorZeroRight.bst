// XOR with zero retains exactly one evaluation of the value.
Kernel XorZeroRight(inherits ConstantFolding);
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
XorRightIdentity :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger { $1 `bit_xor` $2 ~> $1; }
