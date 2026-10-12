# Chapter 18 — The CLI and diagnostics

The Python module entry point exposes `parse`, `check` and `apply`. Use chapter 2's
environment so the module can locate the native parser and optional runtime.

## Command reference

```sh
python3 -m blackstone parse examples/kernels/Reassociate.bst
python3 -m blackstone check examples/kernels
python3 -m blackstone check kernels
python3 -m blackstone apply examples/kernels/Identity.bst \
  examples/add-zero-right.json --trace --engine native
```

| Command/option | Behavior |
| --- | --- |
| `parse FILE` | Prints the source-spanned AST as JSON |
| `check FILE` | Resolves and validates one root; prints local/resolved rule counts |
| `check DIRECTORY` | Recursively loads and validates the catalog |
| `apply FILE HIR.json` | Optimizes the HIR fixture and prints JSON |
| `--kernel-dir DIR` | Recursively loads dependencies; repeat for more directories |
| `--engine python\|native` | Chooses the application driver; default Python |
| `--trace` | Emits successful rewrite events to stderr |
| `--budget N` | Bounds successful rewrites; default 10,000 |

`parse` recognizes syntax and creates an AST; it does not resolve operator,
capability or inheritance contracts. A syntactically valid unknown operator can
therefore appear in parsed output and subsequently fail `check`.

## Output streams and statuses

Successful application writes only optimized JSON to stdout. With `--trace`,
stderr contains JSON-line events with `rule`, `source`, `rule_source`, `before`
and `after`. The `source` field is HIR provenance; `rule_source` is the `.bst`
rule location or `<builtin>`.

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --trace > optimized.json 2> rewrite-trace.jsonl
```

Successful commands exit with status 0. Source, fixture, runtime and I/O diagnostics
handled by the CLI exit with status 1; argument-parser usage errors use its usual
status 2. On nonconvergence, stdout contains no optimized JSON. Stderr includes
completed trace events when requested and then the final text diagnostic, so a
failure stream is not exclusively JSON lines.

## Useful diagnostic stages

* **Syntax:** missing delimiters, malformed operators, unterminated comments or
  backticks, and misplaced declarations.
* **Resolution:** unknown concerns/capabilities/operators/kernels, ambiguous
  imported names, duplicate rules and inheritance cycles.
* **Rule validation:** incorrect capture numbering/range, forward `!same`,
  signature/match tree mismatch, incorrect predicate arity or replacement sort.
* **HIR/runtime:** unknown node IDs, malformed type/fact metadata, cycles,
  representability failures, adapter contract violations and nonconvergence.

Locations use source path, line and column where available. A root with mixed
types or unknown analysis facts generally produces a guard nonmatch, not a source
diagnostic. The corresponding examples intentionally complete successfully
without changing their roots.

## Reproduce a bounded failure

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --budget 1 --trace
```

Reassociation completes; the subsequent folder exceeds the budget. Compare the
event name and pending folder diagnostic with the two-step successful run before
investigating rule order.

[Previous](17-scheduling-and-convergence.md) · [Contents](README.md) · [Next: Python APIs](19-python-frontend-and-adapters.md)
