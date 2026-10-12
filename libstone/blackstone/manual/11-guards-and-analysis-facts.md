# Chapter 11 — Guards and analysis facts

A guard is a nonempty sequence of imported predicates joined by `&&`.
Parentheses group conjunctions. v0 has no disjunction or guard negation syntax.
Predicate arguments are capture references, not arithmetic expressions.

```text
where ModularInteger && Pure($1) && Total($1)
where (ModularInteger && (Pure($1) && Total($1)))
```

These fragments have the same left-to-right query order. Parenthesized
conjunctions flatten without changing that order.

## Three-valued results

Each query returns proven true, proven false or unknown. The Python adapter uses
`True`, `False` and `None`; the C API uses `BS_TRUE`, `BS_FALSE` and `BS_UNKNOWN`.
Only proven true passes. If a conjunct is false or unknown, evaluation stops and
no later conjunct or replacement constructor runs for that candidate.

This makes an unknown fact operationally sufficient to block a rewrite without
claiming the opposite fact. For example, absence of a purity proof does not prove
an effect, but it still prevents an annihilator from discarding the value.

## Root properties versus capture facts

`Associative` and `Commutative` query the matched root's opcode, exact type and
semantic flags. The reference registry establishes them for eligible modular
`plus` and `times`. They do not by themselves establish an identity element or
the eligibility of every nested operator.

`ModularInteger` checks the whole exposed binary skeleton. `Pure($1)` and
`Total($1)` query the specific captured value. The reference adapter derives
conservative binary facts from operator semantics and operand facts, or uses
explicit host-supplied proofs.

## Observe proof gating

```sh
python3 examples/run_examples.py --engine both \
  --case multiply-zero --case effectful-zero \
  --case unknown-facts --case trapping-zero
```

Only the fully pure, total operand can be discarded. In the trapping case,
`Pure` can pass while `Total` fails. All these are metadata queries: the driver
does not run a trapping operation to test whether it traps.

## Supplying an additional fact

The catalog's `DivideSelf` rule also requires `Nonzero($1)`. The reference adapter
can prove this for a concrete nonzero literal; a nonliteral input requires host
analysis. The runnable script supplies a fact for a particular stable input:

```sh
python3 examples/analysis_facts.py --engine native
```

It explicitly assumes the input domain excludes zero, keeps purity/totality as
separate proofs, and changes `x divide x` to typed one. Without the supplied
nonzero fact, the same node remains unchanged. The adapter does not infer facts
from an input's descriptive name.

Guard arities and capture ranges are checked statically. Runtime unknown facts
are ordinary nonmatches; unknown predicate names or missing imports are source
errors. This distinction is important when interpreting CLI diagnostics.

[Previous](10-refinements.md) · [Contents](README.md) · [Next: Integer types](12-types-and-modular-integers.md)
