// A complete first rule: x + 0 -> x at the original modular integer type.
Kernel ExampleIdentity;

concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

KeepLeft :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger
{
    $1 `plus` $2 ~> $1;
}
