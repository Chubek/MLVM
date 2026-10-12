# Blackstone implementation brief

Implement **Blackstone**, a declarative language for specifying compiler rewrite
and optimization rules. Use `kernels/AlgebraicSimplification.bst` as the first end-to-end
acceptance fixture: it contains the original `AddZero` rule and exactly **15 new
rules**, for **16 local rules total**.

The starting point is a prototype, not an existing parser or published language
specification. The refinements, guards, typing, and execution semantics below are
explicit **proposed v0 design decisions** that complete that prototype. Implement
this contract consistently and document any additional decisions. Follow the
implementation repository's language, structure, and tooling conventions.

## 1. Deliverables and implementation order

Build a runnable vertical slice with:

1. A lexer and parser with source spans and a printable AST.
2. Declaration resolution and rule validation.
3. A typed HIR adapter and registries for operators, matchers, and properties.
4. A guarded rewrite engine with deterministic ordering and convergence checks.
5. A minimal inherited `ConstantFolding` kernel and integer literal construction.
6. CLI entry points for parsing, checking, and applying kernels to a documented
   HIR fixture format. Show rule names in an optional rewrite trace.
7. Meaningful parser, negative-diagnostic, semantics, and end-to-end tests.

Keep the public boundaries between the frontend, HIR adapter, and rewrite driver
clear. A small reference adapter is sufficient for the first implementation;
compiler integrations should supply the same contracts. Preserve the companion
fixture's spelling, capture numbering, and meaning.

## 2. Concrete syntax

Identifiers and keywords are case-sensitive. Recognize `//` line comments and
non-nesting `/* ... */` block comments. Whitespace and comments may occur between
tokens, including inside multiline signatures. Unterminated comments and
backticks are diagnostics.

The following EBNF defines the required subset. Quoted strings are tokens;
`{ ... }` means repetition and `[ ... ]` means optional syntax.

```ebnf
file          = kernelDecl, { concernDecl | requireDecl }, { ruleDecl } ;
kernelDecl    = "Kernel", identifier,
                [ "(", "inherits", qualifiedName,
                  { ",", qualifiedName }, ")" ], ";" ;
concernDecl   = "concerns", qualifiedName, ";" ;
requireDecl   = "requires", importSpec, ";" ;
importSpec    = qualifiedName
              | qualifiedName, "::", "{", identifier,
                { ",", identifier }, "}" ;
qualifiedName = identifier, { "::", identifier } ;

ruleDecl      = identifier, "::", pattern, "->", sort,
                "where", guard, "{", matchExpr, "~>", buildExpr, ";", "}" ;
sort          = "Term" | "Binary" ;
pattern       = "Binary", "(", pattern, ",", pattern, ")"
              | "Term", "!", refinement ;
refinement    = "resolves" | "literal" | "nonliteral"
              | "exact", "(", integer, ")"
              | "same", "(", capture, ")" ;

guard         = guardAtom, { "&&", guardAtom } ;
guardAtom     = identifier, [ "(", capture, { ",", capture }, ")" ]
              | "(", guard, ")" ;
matchExpr     = matchAtom, { operator, matchAtom } ;
matchAtom     = capture | "(", matchExpr, ")" ;
buildExpr     = buildAtom, { operator, buildAtom } ;
buildAtom     = capture | integer | "(", buildExpr, ")" ;
operator      = "`", identifier, "`" ;
capture       = "$", positiveInteger ;
integer       = [ "-" ], decimalDigits ;
```

Use ASCII identifiers matching `[A-Za-z_][A-Za-z0-9_]*`, decimal digits, and
one-based capture indices. Parse integer spelling without machine-integer
overflow; representability is a typed matching/construction decision. A
qualified-name parser must distinguish `:: Identifier` from the `:: {` import
group suffix.

All backtick infix operators have the same precedence and associate left-to-right
in this subset. Parentheses explicitly establish the nested trees used by the
fixture. Backtick names are registered HIR operators, not host-language code.
Each rule has exactly one directed rewrite. `->` declares the result sort;
`~>` separates the match expression from the replacement expression.

## 3. Declarations and symbol resolution

- `Kernel AlgebraicSimplification(inherits ConstantFolding);` names the kernel
  and its dependencies. Resolve inherited kernels and reject inheritance cycles.
- `concerns` registers HIR views used by pattern sorts. For this prototype,
  `HIR::Literals::Term` supplies the language-facing **typed value** view;
  `HIR::Expressions::Binary` supplies the binary-expression view. The seed's
  `Term` is deliberately broader than a literal: `Binary` results are also
  `Term` values. Literal restrictions come from refinements. Make that adapter
  mapping explicit rather than inferring it from a host class hierarchy.
- `requires` imports capability contracts. Grouped imports expand into individual
  qualified names. Missing modules/capabilities are compile-time diagnostics;
  insufficient facts about a particular HIR value simply make a match fail.
- Resolve imported short names without ambiguity. The predicate aliases
  `Associative` and `Commutative` refer to imported `Associativity` and
  `Commutativity`, respectively. Other fixture predicates retain their imported
  names. Pattern refinements are built-ins, not imported predicates.
- Reject unknown operators, predicates, sorts, kernels, and refinements. Reject
  duplicate rule names across a kernel's resolved inheritance graph; there is
  no implicit override. An unused capability import is permitted.

## 4. Captures, patterns, and matching

Number **Term leaves of the signature** in depth-first, left-to-right order,
starting at `$1` independently in every rule. A `Binary` pattern contributes
structure, not a capture slot. Every leaf binds a capture, including `!same`.

For example:

```text
Binary(Binary(Term!nonliteral, Term!literal), Term!literal)
              $1               $2             $3
```

The match expression assigns an opcode to each `Binary` in the signature and
must have exactly the same binary/capture tree. Each leaf must reference its
assigned capture exactly once and in its assigned position. Consequently,
`($1 plus $2) plus $3` is structurally different from `$1 plus ($2 plus $3)`;
the actual source spells each operator in backticks. A captured Term may itself
be an expression without exposing its internal structure in the signature.

Refinements have these meanings:

| Refinement | Contract |
| --- | --- |
| `Term!resolves` | Match an existing HIR value whose sort and type are resolved. This is not a request to evaluate or constant-fold it. |
| `Term!literal` | Match a resolved, concrete typed literal node/value. A merely constant-foldable expression is not a literal. |
| `Term!nonliteral` | Match a resolved Term that is not a concrete literal. |
| `Term!exact(n)` | Match a concrete literal whose numeric value equals representable integer `n` in its actual type. Do not silently reduce `n` modulo the width. |
| `Term!same($n)` | Match a resolved Term with the same type and stable HIR value identity as an earlier capture. Bind the current slot too. |

`!same` may reference only an earlier slot. Validate all capture references in
refinements, guards, and replacements. `$0`, out-of-range references, forward
identity references, and a mismatched signature/match tree are errors.

Value identity means the same SSA value ID, or the same expression-node identity
under an explicitly documented tree adapter. Similar source text or structural
equality is insufficient. Identity alone does not justify removing evaluations:
the self-cancellation rules also require `Pure` and `Total`.

Matching and guard queries inspect HIR metadata and analysis facts. They never
execute the source program. Complete matching and guard evaluation before
constructing or installing a replacement.

## 5. Types, integer semantics, and properties

Every matched operator has a concrete result type `T`. Preserve that exact type
through replacement; `-> Term` does not allow a change in width or signedness.
The fixture's `ModularInteger` predicate requires homogeneous scalar integer
types across all operands/results of the explicitly matched binary skeleton,
with width `W >= 1` and a consistent signed or unsigned interpretation.
Captured subtrees retain their own internal typing and semantics.

Register these operator meanings:

| Backtick name | Integer meaning |
| --- | --- |
| `plus` | Addition modulo `2^W`. |
| `minus` | Subtraction modulo `2^W`. |
| `times` | Multiplication modulo `2^W`. |
| `divide` | Unsigned quotient, or signed quotient truncated toward zero. Division by zero and signed minimum divided by negative one are exceptional. |
| `bit_and`, `bit_or`, `bit_xor` | Bitwise operations on the same `W`-bit representation. |

`ModularInteger` rejects floating-point, pointer, Boolean, and vector operands.
Addition, subtraction, and multiplication must wrap: their saturating,
checked-overflow, and overflow-poison variants are excluded. In particular,
`nsw`/`nuw`-like flags must not be treated as ordinary wrapping operations.
Division retains the exceptional cases in the operator table. Check every
operator node exposed by a nested signature, not only its root, and reject flags
that change the registered semantics. The captured Term itself need not be pure
or total unless a separate guard requires it.

An unadorned replacement `0` constructs a literal of the original result type
`T`. `Term!exact(1)` matches positive numeric one, not an arbitrary bit pattern
containing a low bit. Thus a signed one-bit value with bits `1` means negative
one and cannot match `!exact(1)`.

Guard contracts:

- `Associative` / `Commutative`: query the corresponding property of the matched
  root operator, concrete type, and semantic flags. The reference adapter may
  establish these properties for modular `plus` and `times`. Associativity alone
  does not establish an identity element; `AddZero` also needs the explicit
  integer-domain contract.
- `Pure($n)`: the captured value/expression is referentially transparent:
  evaluations are stable and have no observable effects or nondeterminism.
  Volatile/atomic accesses, state-dependent repeated loads, and effectful calls
  are not certified pure without appropriate host proofs.
- `Total($n)`: evaluation terminates and yields a defined value for all inputs
  admitted by the current analysis facts, with no exception, trap, undefined
  behavior, poison, or independently varying undef value.

Predicates return proven true, proven false, or unknown. A guard succeeds only
when all conjuncts are proven true. Evaluate `&&` left-to-right with
short-circuiting. Predicate argument counts are checked statically.

An SSA reference is a read of an already-produced value, not a request to
reevaluate its defining instruction. Its purity/totality facts must reflect that
distinction and any undefined-value semantics. Host dead-code elimination must
preserve effectful definitions and effect dependencies after uses are rewritten.

## 6. Replacement construction

Reused captures preserve their value identity, type, and provenance. Newly built
binary nodes use the same concrete type and the operator semantics proved by the
match. The reassociation rules introduce a literal/literal binary subtree; they
do not request a host-language calculation or a different-width intermediate.

Carry source/debug provenance and the originating rule name into rewrite traces.
Transfer semantic flags only when their validity is established for the new
node. Construct replacements without mutating the original match; install them
atomically after validation. Skip an identity/no-change replacement.

For a tree adapter, operand occurrences represent evaluations. For an SSA
adapter, update uses and requeue affected users through its def-use machinery.
Keep those adapter semantics explicit when comparing results or analyzing
effects.

## 7. Inheritance, scheduling, and convergence

Resolve inherited kernels base-first, in the listed dependency order, then the
current kernel. Visit each kernel once so a shared ancestor is not applied
twice. Within each kernel use source declaration order. Visit operand subtrees
before their parent and use the first matching rule in the combined ordered
rule list.

After a successful rewrite, revisit the replacement's children and restart the
combined rule list on it. Continue until the affected expression and its users
reach a fixed point. In particular:

```text
(x plus 3) plus 4
  -- ReassociateAddConstants --> x plus (3 plus 4)
  -- inherited ConstantFolding --> x plus 7
```

The example assumes a sufficiently wide integer type and a nonliteral `x`.
Constants wrap in narrower types according to that type's semantics. If a
folded constant is zero or one, restart local simplification so its identity or
annihilator rule can run.

Supply a minimal built-in `ConstantFolding` kernel if the implementation has no
existing one. Fold concrete literal/literal operations from the operator table
at their exact type. Leave exceptional division intact. Inherited rules are not
part of the fixture's count of 16 local rules.

The supplied rules are deliberately oriented. `AddConstantRight` requires a
literal left operand and a nonliteral right operand; it cannot match its own
result. Reassociation groups right-side literals and has no reverse rule.

A termination argument for the reference expression-tree adapter is the
lexicographic measure `(B, L, D)`:

1. `B`: total number of binary nodes in the expression tree.
2. `L`: number of `plus` nodes with literal left/nonliteral right operands.
3. `D`: sum of left-subtree sizes over `plus`/`times` nodes whose right operand
   is a literal; subtree size counts all nodes, including literals.

Identity/annihilator rules and constant folding decrease `B`. Constant-right
normalization preserves `B` and decreases `L`. Reassociation preserves `B` and
`L` and strictly decreases `D`. For a finite acyclic SSA graph, use the conceptual
unfolded tree for this argument. Reject cyclic expression input to the reference
adapter. Also enforce a configurable rewrite budget for arbitrary user kernels;
budget exhaustion must produce a useful nonconvergence diagnostic and trace.

## 8. Fixture inventory and correctness obligations

| Rule | Rewrite | Additional proof beyond `ModularInteger` |
| --- | --- | --- |
| `AddZero` | `0 + x -> x` | Associativity retained from the seed. |
| `AddZeroRight` | `x + 0 -> x` | None. |
| `SubtractZero` | `x - 0 -> x` | None. |
| `SubtractSelf` | `x - x -> 0` | Same identity; pure, total `x`. |
| `MultiplyOne` | `1 * x -> x` | Representable positive one. |
| `MultiplyOneRight` | `x * 1 -> x` | Representable positive one. |
| `MultiplyZero` | `0 * x -> 0` | Pure, total discarded `x`. |
| `MultiplyZeroRight` | `x * 0 -> 0` | Pure, total discarded `x`. |
| `DivideOne` | `x / 1 -> x` | Representable positive one. |
| `AndZero` | `x & 0 -> 0` | Pure, total discarded `x`. |
| `AndSelf` | `x & x -> x` | Same identity; pure, total `x`. |
| `OrZero` | `x \| 0 -> x` | None. |
| `XorSelf` | `x ^ x -> 0` | Same identity; pure, total `x`. |
| `AddConstantRight` | `c + x -> x + c` | Commutativity; literal `c`, nonliteral `x`. |
| `ReassociateAddConstants` | `(x + c1) + c2 -> x + (c1 + c2)` | Associativity; nonliteral `x`, literal constants. |
| `ReassociateMultiplyConstants` | `(x * c1) * c2 -> x * (c1 * c2)` | Associativity; nonliteral `x`, literal constants. |

Before declaring the implementation complete, verify:

- Parse and validate all 16 rules, including multiline signatures, aliases,
  nested patterns, comments, and one-based captures.
- Diagnose unknown symbols, malformed delimiters, invalid capture indices,
  forward `!same` references, mismatched pattern trees, duplicate names, and
  inheritance cycles with actionable source locations.
- Exercise every rule through the actual matcher and driver; assert exact
  result types as well as values. Use exhaustive small-width or property-based
  comparisons against a reference integer evaluator, including wraparound and
  signed/unsigned interpretations.
- Demonstrate nonmatches for mixed types, unsupported arithmetic domains,
  altered overflow flags, missing analysis facts, and signed one-bit negative
  one masquerading as positive one.
- Use counterexamples with an effectful operand, an operand that traps or
  diverges, poison/undef, and two structurally equal but identity-distinct
  expressions. Verify the relevant erasure/self rules decline those matches.
- Show identities that retain an effectful operand preserve exactly one
  evaluation. Check constant movement and reassociation preserve its evaluation
  count and observable order relative to other nonliteral evaluations.
- Show inheritance-driven folding after reassociation, fixed-point identity
  cleanup, deterministic traces, idempotence, and bounded convergence.

Document how to run these checks and how a compiler integration supplies typed
values, value identity, operator semantics, analysis facts, and replacement
construction. Finish with a working demonstration that loads the actual
`AlgebraicSimplification.bst` file and optimizes typed HIR input.
