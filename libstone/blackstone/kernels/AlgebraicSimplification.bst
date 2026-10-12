// ============================================================================
// Blackstone :: AlgebraicSimplification
// The original AddZero rule, completed, plus fifteen additional rules.
// Language and host-adapter contract: AGENTS_.md (proposed Blackstone v0).
// ============================================================================
//
// Scope: homogeneous scalar, fixed-width modular integers. Addition,
// subtraction, and multiplication must wrap rather than check or saturate
// overflow or produce overflow poison. Division retains its exceptional cases.
//
// Term captures are numbered left-to-right through the signature's leaves.
// !same($n) requires value identity; it does not mean textual similarity.
// Pure and Total protect rules that discard an operand or a repeated evaluation.
// Numeric replacements inherit the matched expression's exact result type.

Kernel AlgebraicSimplification(inherits ConstantFolding);

concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;

requires Relational::Properties::{Associativity, Commutativity};
requires Arithmetic::Domains::ModularInteger;
requires HIR::Effects::{Pure, Total};

// --- Additive identities -----------------------------------------------------

// Associative is retained from the seed; ModularInteger establishes that
// `plus` is integer addition and that its exact zero is an additive identity.
AddZero :: Binary(Term!exact(0), Term!resolves) -> Term
where Associative && ModularInteger
{
   $1 `plus` $2 ~> $2;
}

AddZeroRight :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger
{
   $1 `plus` $2 ~> $1;
}

SubtractZero :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger
{
   $1 `minus` $2 ~> $1;
}

// Both occurrences must denote the same stable, defined, total value.
SubtractSelf :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1)
{
   $1 `minus` $2 ~> 0;
}

// --- Multiplicative identities and annihilators ------------------------------

MultiplyOne :: Binary(Term!exact(1), Term!resolves) -> Term
where ModularInteger
{
   $1 `times` $2 ~> $2;
}

MultiplyOneRight :: Binary(Term!resolves, Term!exact(1)) -> Term
where ModularInteger
{
   $1 `times` $2 ~> $1;
}

// Discarding x must preserve its effects, exceptions, and definedness.
MultiplyZero :: Binary(Term!exact(0), Term!resolves) -> Term
where ModularInteger && Pure($2) && Total($2)
{
   $1 `times` $2 ~> $1;
}

MultiplyZeroRight :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger && Pure($1) && Total($1)
{
   $1 `times` $2 ~> $2;
}

// A positive, representable one avoids division's exceptional cases.
DivideOne :: Binary(Term!resolves, Term!exact(1)) -> Term
where ModularInteger
{
   $1 `divide` $2 ~> $1;
}

// --- Bitwise identities -----------------------------------------------------

AndZero :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger && Pure($1) && Total($1)
{
   $1 `bit_and` $2 ~> $2;
}

AndSelf :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1)
{
   $1 `bit_and` $2 ~> $1;
}

OrZero :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger
{
   $1 `bit_or` $2 ~> $1;
}

XorSelf :: Binary(Term!resolves, Term!same($1)) -> Term
where ModularInteger && Pure($1) && Total($1)
{
   $1 `bit_xor` $2 ~> 0;
}

// --- Canonical form: an additive literal belongs on the right ----------------

// Requiring a nonliteral right operand prevents a swap/swap-back cycle.
// Moving a literal past x preserves x's single evaluation.
AddConstantRight :: Binary(Term!literal, Term!nonliteral) -> Term
where ModularInteger && Commutative
{
   $1 `plus` $2 ~> $2 `plus` $1;
}

// --- Expose adjacent constants to the inherited ConstantFolding kernel --------

// The inner Binary does not consume a capture slot: x=$1, c1=$2, c2=$3.
// These rules preserve operand order and never duplicate or discard x.
ReassociateAddConstants ::
   Binary(Binary(Term!nonliteral, Term!literal), Term!literal) -> Term
where ModularInteger && Associative
{
   ($1 `plus` $2) `plus` $3 ~> $1 `plus` ($2 `plus` $3);
}

ReassociateMultiplyConstants ::
   Binary(Binary(Term!nonliteral, Term!literal), Term!literal) -> Term
where ModularInteger && Associative
{
   ($1 `times` $2) `times` $3 ~> $1 `times` ($2 `times` $3);
}
