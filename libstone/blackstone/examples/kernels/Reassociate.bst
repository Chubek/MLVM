/* Parentheses, multiline signatures, grouped imports, and predicate aliases.
 * This kernel exposes a literal pair to its inherited constant folder. */
Kernel ExampleReassociate(inherits ConstantFolding);

concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Relational::Properties::{Associativity, Commutativity};

GroupRightConstants ::
    Binary(
        Binary(Term!nonliteral, Term!literal),
        Term!literal
    ) -> Binary
where (ModularInteger && Associative)
{
    ($1 `plus` $2) `plus` $3 ~> $1 `plus` ($2 `plus` $3);
}
