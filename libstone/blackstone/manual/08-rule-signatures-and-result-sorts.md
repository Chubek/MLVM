# Chapter 8 — Rule signatures and result sorts

A rule declares one signature, one result sort, a nonempty guard and one directed
rewrite. The subset has this outer structure:

```ebnf
ruleDecl = identifier, "::", pattern, "->", sort,
           "where", guard, "{", matchExpr, "~>", buildExpr, ";", "}";
sort     = "Term" | "Binary";
pattern  = "Binary", "(", pattern, ",", pattern, ")"
         | "Term", "!", refinement;
```

`::` separates the rule name from its signature. `->` declares the result sort.
`~>` directs the actual transformation inside the body. They are not interchangeable
arrows, and a body contains exactly one rewrite followed by a semicolon.

## Signatures expose structure

The signature tells the matcher which binary nodes to inspect and which Term
leaves to bind. Binary nodes contribute structure but no capture slots. Every
Term has a refinement, including a broadly accepting `!resolves`.

The match expression supplies an opcode for every exposed Binary and the assigned
capture at every leaf. Its tree must exactly match the signature. Operator
grouping and capture positions are therefore checked before execution.

## A binary result

This complete example normalizes a literal to the right:

```bst
Kernel ExampleBinaryResult;
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;
requires Relational::Properties::Commutativity;

MoveLiteral :: Binary(Term!literal, Term!nonliteral) -> Binary
where ModularInteger && Commutative
{
    $1 `plus` $2 ~> $2 `plus` $1;
}
```

The replacement explicitly constructs a Binary. Its exact arithmetic type comes
from the matched root; `Binary` does not specify width or signedness. The literal/
nonliteral orientation prevents this rule from matching its own result.

## Term results and capture forwarding

`-> Term` permits forwarding a capture, constructing a literal, or constructing
a binary expression because all are typed values in the adapter's Term view.
It still requires exact result-type preservation. A general Term capture might
hold an input or literal, so it cannot statically satisfy `-> Binary` by itself.
Such a rule is rejected with a binary-construction diagnostic.

The reference subset does not provide additional result sorts, variadic patterns,
alternatives or multiple body statements. A compiler integration expresses richer
host structures through registered descriptors and these same Term/Binary sorts.

## Authoring checks

Before applying a new rule, run `check` and verify that:

* Its concerns declare every pattern/result sort.
* Every Term has the intended refinement.
* The match tree has exactly the signature's shape and leaf numbering.
* Every backtick name is registered, and every guard is imported with the right
  argument count.
* The replacement can preserve the exact matched type.

The runnable [Reassociate example](../examples/kernels/Reassociate.bst) uses a
nested signature and `-> Binary`; chapter 9 explains its capture numbering.

## Grammar reference

This is the validated v0 source subset. Braces mean repetition, brackets mean
optional syntax, and quoted items are tokens. Whitespace/comments can separate
tokens. `identifier` and `decimalDigits` use chapter 4's ASCII lexical rules.

```ebnf
file          = kernelDecl, { concernDecl | requireDecl }, { ruleDecl };
kernelDecl    = "Kernel", identifier,
                [ "(", "inherits", qualifiedName,
                  { ",", qualifiedName }, ")" ], ";";
concernDecl   = "concerns", qualifiedName, ";";
requireDecl   = "requires", importSpec, ";";
importSpec    = qualifiedName
              | qualifiedName, "::", "{", identifier,
                { ",", identifier }, "}";
qualifiedName = identifier, { "::", identifier };

ruleDecl      = identifier, "::", pattern, "->", sort,
                "where", guard, "{", matchExpr, "~>", buildExpr, ";", "}";
sort          = "Term" | "Binary";
pattern       = "Binary", "(", pattern, ",", pattern, ")"
              | "Term", "!", refinement;
refinement    = "resolves" | "literal" | "nonliteral"
              | "exact", "(", integer, ")"
              | "same", "(", capture, ")";

guard         = guardAtom, { "&&", guardAtom };
guardAtom     = identifier, [ "(", capture, { ",", capture }, ")" ]
              | "(", guard, ")";
matchExpr     = matchAtom, { operator, matchAtom };
matchAtom     = capture | "(", matchExpr, ")";
buildExpr     = buildAtom, { operator, buildAtom };
buildAtom     = capture | integer | "(", buildExpr, ")";
operator      = "`", identifier, "`";
capture       = "$", decimalDigits;
integer       = [ "-" ], decimalDigits;
```

Capture numeric values must be positive and within their rule's leaf range;
validation checks these conditions. The parser also accepts identifier spellings
for names that the resolver later rejects as unknown sorts or refinements. A
successful parse is therefore only the first stage of validating this subset.

[Previous](07-capabilities-and-symbol-resolution.md) · [Contents](README.md) · [Next: Captures](09-captures-and-pattern-structure.md)
