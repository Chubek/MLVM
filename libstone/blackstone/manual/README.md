# The Blackstone manual

This manual describes the implemented **Blackstone v0 language and ABI v1**.
It follows a rule from source text through validation, typed matching,
construction and compiler integration. Each numbered chapter is a separate
Markdown file; this page is the table of contents.

All shell commands run from `libstone/blackstone` unless a chapter says otherwise.
Build and environment setup are in chapter 2. Executable examples live in
[`../examples`](../examples/README.md), with expected outputs recorded in
[`cases.json`](../examples/cases.json). Mathematical expressions in prose use
plain operator names; actual `.bst` infix operators are surrounded by backticks.

## Part I — Starting and declaring contracts

1. [What Blackstone does](01-introduction.md)
2. [Building and running the tools](02-build-and-tooling.md)
3. [Your first rewrite](03-first-rewrite.md)
4. [Lexical syntax and source locations](04-lexical-syntax.md)
5. [Kernels and inheritance](05-kernels-and-inheritance.md)
6. [Concerns and typed HIR views](06-concerns-and-hir-views.md)
7. [Capabilities and symbol resolution](07-capabilities-and-symbol-resolution.md)
8. [Rule signatures and result sorts](08-rule-signatures-and-result-sorts.md)

## Part II — Matching and preserving semantics

9. [Captures and pattern structure](09-captures-and-pattern-structure.md)
10. [Term refinements](10-refinements.md)
11. [Guards and analysis facts](11-guards-and-analysis-facts.md)
12. [Types and modular integers](12-types-and-modular-integers.md)
13. [Operators and constant folding](13-operators-and-constant-folding.md)
14. [Effects, totality and value identity](14-effects-totality-and-value-identity.md)
15. [Replacement construction](15-replacement-construction.md)
16. [HIR fixtures and provenance](16-hir-fixtures-and-provenance.md)

## Part III — Execution and integration

17. [Scheduling and convergence](17-scheduling-and-convergence.md)
18. [The CLI and diagnostics](18-command-line-and-diagnostics.md)
19. [Python frontends and adapters](19-python-frontend-and-adapters.md)
20. [The native runtime and modules](20-native-runtime-and-modules.md)
21. [Native IR, profiles and persistence](21-native-ir-profiles-and-persistence.md)
22. [Structured-host kernels](22-structured-host-kernels.md)
23. [Testing and correctness](23-testing-and-correctness.md)
24. [Compiler integration and extension](24-compiler-integration-and-extension.md)

## Reading paths

* **Try a rewrite:** chapters 1–3, 16 and 18.
* **Write integer rules:** chapters 4–15, 17 and 23.
* **Embed the Python adapter:** chapters 14–19 and 24.
* **Embed the C runtime:** chapters 14–17, 20–21 and 24.
* **Work on loops, memory or control flow:** chapters 6–7, 11, 14 and 22–24.

The [native API guide](../docs/native-api.md) is a compact companion reference;
the [kernel catalog](../kernels/CATALOG.md) lists the supplied transformations.
