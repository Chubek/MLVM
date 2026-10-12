# Chapter 7 — Capabilities and symbol resolution

`requires` imports capability contracts used by guards. A contract establishes a
predicate's registered identity, short name or alias, and argument count. Importing
it makes the query available; the host's facts determine whether it succeeds for
a particular match.

## Individual and grouped imports

```bst
Kernel ExampleImports;
requires Arithmetic::Domains::ModularInteger;
requires Relational::Properties::{Associativity, Commutativity};
requires HIR::Effects::{Pure, Total};
```

The group expands into individual qualified names. For example,
`HIR::Effects::{Pure, Total}` imports `HIR::Effects::Pure` and
`HIR::Effects::Total`. The parser distinguishes a qualified-name segment
`:: Identifier` from the grouped suffix `:: { ... }`.

Imports may be unused. Missing modules/capabilities are diagnosed while resolving
the source. In contrast, an existing query with insufficient facts returns
unknown at matching time and causes that candidate to fail.

## Acceptance capabilities

| Qualified capability | Guard spelling | Arguments |
| --- | --- | ---: |
| `Arithmetic::Domains::ModularInteger` | `ModularInteger` | 0 |
| `Relational::Properties::Associativity` | `Associative` or `Associativity` | 0 |
| `Relational::Properties::Commutativity` | `Commutative` or `Commutativity` | 0 |
| `HIR::Effects::Pure` | `Pure` | 1 |
| `HIR::Effects::Total` | `Total` | 1 |

The two property aliases are explicit registry entries. Other fixture predicates
retain their imported names. Zero-argument predicates appear as bare names;
`Pure($1)` and `Total($1)` name captured values. The concrete grammar does not
accept an empty argument list such as `ModularInteger()`.

The expression catalog adds `Arithmetic::Values::AllOnes` and
`Arithmetic::Values::Nonzero`, each taking one capture. Structured-host modules
add whole-transformation proofs and capture facts; chapter 22 describes them.

## Ambiguity and local scope

Imported short names must be unambiguous. If two distinct capabilities export
the short name `Pure`, the resolver cannot choose one silently, even if one also
has a different alias. Repeated references to the same qualified contract do not
introduce a different meaning.

Local rules resolve their guards against their own kernel's imports. Inherited
rules retain the imports of their declaring kernel. A name being present in the
global registry does not make it usable in a rule that has not imported it.

## Independent namespaces

Concerns, capabilities, operators, inherited kernels and refinements have separate
contracts. Backtick `plus` resolves as an operator; `ModularInteger` resolves as
an imported predicate; `literal` resolves as a built-in refinement. Importing a
capability does not register a new operator or redefine `Term!literal`.

**Exercise:** remove the `ModularInteger` import from a copy of
`examples/kernels/Identity.bst` and run `check`. The guard is diagnosed as unknown
or unimported. Restoring the import makes the rule valid, but applying it to
`examples/mixed-types.json` still does not match: a valid contract is not a proof
for an incompatible value.

[Previous](06-concerns-and-hir-views.md) · [Contents](README.md) · [Next: Rule signatures](08-rule-signatures-and-result-sorts.md)
