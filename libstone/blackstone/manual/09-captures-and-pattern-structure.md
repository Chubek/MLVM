# Chapter 9 — Captures and pattern structure

Capture numbering starts independently at `$1` for each rule. Number the Term
leaves in depth-first, left-to-right order. A Binary contributes no slot, and a
Term with `!same` still contributes its own slot.

## Number a nested pattern

The reassociation signature is:

```text
Binary(Binary(Term!nonliteral, Term!literal), Term!literal)
              $1               $2             $3
```

Its match expression is:

```text
($1 `plus` $2) `plus` $3
```

The outer Binary's left child is itself a Binary. The expression
`$1 plus ($2 plus $3)` would require the Binary on the right instead and is not
a valid match tree for this signature. It is, however, the intended replacement:
construction is allowed to create a different tree.

Every signature leaf must appear exactly once, at its assigned match position.
Swapping `$1` and `$2`, repeating a match capture, or skipping a slot is a static
error. The replacement can reuse, reorder or omit captures when the rule's
semantic proof justifies it.

## Identity leaves also bind

The factoring catalog uses:

```text
Binary(
    Binary(Term!resolves, Term!resolves),
    Binary(Term!resolves, Term!same($2))
)
          $1              $2
          $3              $4, same identity as $2
```

Its match is `($1 times $2) plus ($3 times $4)`. The fourth leaf binds `$4` even
though its refinement requires identity with `$2`. The replacement
`($1 plus $3) times $2` refers to the common value directly.

Run this through the actual catalog rule:

```sh
python3 -m blackstone apply kernels/expressions/FactorCommonRight.bst \
  examples/factor-common-right.json --trace
```

The output preserves the order of the two effectful inputs. The common factor
has one stable identity and is certified pure and total, justifying removal of
one of its occurrences.

## Opaque captures

A Term can contain an entire subtree. Binding `$1` does not expose all of that
subtree's internal operators to the rule. `ModularInteger` checks the signature's
explicit binary skeleton and its immediate operand/result types. A separate
`Pure($1)` or `Total($1)` query may inspect the captured expression more deeply
through the adapter's conservative analysis.

## Reference validation

Capture references in refinements, guards and replacements are checked against
the number of leaves. `$0` and out-of-range references are errors. `!same($n)`
must point backward to an earlier slot, so the first leaf cannot use it.

**Exercise:** draw the signature tree before writing a three-operand match.
Label leaves first, then label Binary opcodes. This makes both the tree-shape
requirement and the distinction between matching and replacement structure
visible before the parser checks them.

[Previous](08-rule-signatures-and-result-sorts.md) · [Contents](README.md) · [Next: Refinements](10-refinements.md)
