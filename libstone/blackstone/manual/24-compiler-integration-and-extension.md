# Chapter 24 — Compiler integration and extension

A compiler pass embeds Blackstone by supplying a typed, analysis-aware HIR
boundary and an installation policy. The frontend, adapter and rewrite driver
remain distinct so each can be tested without assuming a particular compiler's
class hierarchy or mutable IR layout.

## Map the host's semantics explicitly

Begin with a small expression domain. Define how the host supplies:

1. Resolved value sorts and exact types, including all host type distinctions.
2. Stable identities, using SSA IDs or documented expression-node handles.
3. Opcodes and semantic flags that distinguish wrapping from altered operations.
4. Concrete typed literals, with representable numeric spellings.
5. Tri-state properties and analysis facts in the current compilation context.
6. Immutable candidate construction and debug/provenance transfer.

For a direct C host, use a canonical `type_key` when domain/width/signedness alone
do not distinguish all relevant types. Keep handles/view strings alive for the
entire run, and retain the registry until every compiled profile is destroyed.

## A pass-level workflow

```text
load/validate kernel sources or a versioned profile
    -> establish registry and adapter contracts
    -> acquire the applicable host analysis facts
    -> match and prove a candidate
    -> construct and validate its exact-typed replacement
    -> install atomically
    -> repair dependent host metadata and invalidate affected facts
    -> requeue affected expressions/users under the pass budget
```

The reference and native `optimize` drivers perform immutable occurrence-tree
normalization. A mutable SSA integration supplies its own use-update and requeue
layer. Reads of produced SSA values do not reevaluate their defining instructions;
fact queries and dead-code elimination must reflect that distinction. Effectful
definitions and effect dependencies survive rewrites of their uses.

## Extend a contract coherently

A new registered operator needs matching, typing, semantic-flag, property,
purity/totality and construction contracts. If it is foldable, the host also
supplies its exact-type folding behavior and exceptional cases. Registering a
name alone does not establish these semantics. The supplied reference arithmetic
implementation defines the seven acceptance operators; extending it requires
corresponding adapter/evaluator changes.

New capability names declare aliases and arities, with a host implementation
that returns true, false or unknown. Structured transformations additionally
identify a whole-transformation proof domain. A new Python catalog domain needs
its view/operator/proof vocabulary reflected in registry and validation code;
the direct C profile compiler uses registered carrier/proof domains.

## Validate the host boundary

Start with the forwarding example, then literal construction, folding and nested
reassociation. Add negative cases for flags, mixed types, missing facts,
identity-distinct values and exceptional operands. Compare observable behavior
under the host's evaluation model and validate installed IR with the compiler's
own verifier. Keep rule-source traces attached to host debug locations so a
misapplied contract can be reproduced.

Profile caches include source and semantic contract versions in their host-defined
keys. Analysis results also depend on program state and must be invalidated after
installation. Independent-context facts should not be transferred by display
name or stale handle association.

## Continue from the examples

[`programmatic.py`](../examples/programmatic.py) establishes typed construction
and fixed-point identity. [`analysis_facts.py`](../examples/analysis_facts.py)
adds an explicit stable-value proof. [`native_profile.c`](../examples/native_profile.c)
demonstrates C profile ownership/persistence and forwarding. The
[native API guide](../docs/native-api.md) and [catalog](../kernels/CATALOG.md)
provide the compact interfaces and transformation-specific obligations for
building a larger pass.

[Previous](23-testing-and-correctness.md) · [Contents](README.md)
