// Deliberately unoriented: use --budget to observe nonconvergence.
// The rule is equivalent for modular addition but has no canonical direction.
Kernel ExampleSwap;
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

SwapOperands :: Binary(Term!resolves, Term!resolves) -> Binary
where ModularInteger { $1 `plus` $2 ~> $2 `plus` $1; }
