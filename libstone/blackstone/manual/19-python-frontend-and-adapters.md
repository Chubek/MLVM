# Chapter 19 — Python frontends and adapters

The Python package separates source processing, contract resolution, HIR and
rewrite scheduling. The public entry points are:

| Module | Main API |
| --- | --- |
| `blackstone.frontend` | `parse`, `load`, `ast_json`, immutable AST dataclasses, `Diagnostic` |
| `blackstone.resolve` | `Registry`, `resolve`, resolved `Program` |
| `blackstone.hir` | `Type`, `Value`, `literal`, `binary`, `TreeAdapter`, fixture loading/dumping |
| `blackstone.rewrite` | `Driver.optimize`, trace events, `Nonconvergence` |
| `blackstone.native` | Native `Driver` with the same optimize/trace boundary |

The frontend invokes the native parser and converts its concrete tree; it does
not tokenize or reparse the source with another grammar. AST integer spellings
remain strings. `resolve` validates the graph and produces the ordered rules,
registry and resolved source kernels used by native lowering.

## Construct HIR directly

This complete Python fragment uses the actual acceptance source:

```python
from blackstone.frontend import load
from blackstone.hir import Type, Value, binary, literal
from blackstone.resolve import resolve
from blackstone.rewrite import Driver, describe

program = resolve(load("kernels/expressions/AlgebraicSimplification.bst"))
t = Type(width=8)
x = Value("input", t, "x", pure=True, total=True)
root = binary("plus", binary("plus", x, literal(250, t)), literal(6, t))
driver = Driver(program)
result = driver.optimize(root)
assert result is x
print(describe(result))
print([event.rule for event in driver.trace])
```

It prints `x` and the reassociation/folding/identity sequence. For a complete
script that serializes the result and accepts either engine, run:

```sh
python3 examples/programmatic.py --engine python
python3 examples/programmatic.py --engine native
```

Native driver objects own compiled profiles and registries. Use a context manager
or call `close()` when finished. Their traces are Python-owned events and remain
available after closing. Optimizing a closed native driver raises a diagnostic.

## The adapter boundary

`TreeAdapter.identity` compares stable node identity and exact type.
`predicate` receives a name, root, selected capture arguments, explicit binary
skeleton and registry. `fact` derives purity/totality conservatively. `literal`
and `binary` construct immutable typed values with provenance and origin.
`can_construct` checks the reference driver's construction eligibility.

A host-specific adapter can add facts while delegating standard queries:

```sh
python3 examples/analysis_facts.py --engine native
```

That script's `NonzeroFacts` subclass certifies only an explicitly supplied stable
input. It delegates all other predicates to `TreeAdapter`. Both drivers still
require the rule's separate modular, purity and totality guards.

## Errors and reproducibility

Source/contract errors raise `Diagnostic`; budget exhaustion raises
`Nonconvergence` with completed trace events. The native bridge catches Python
callback exceptions and rethrows them after the C call, rather than letting an
exception cross the C ABI. Each `optimize` call resets its run trace and memoization.

Keep host facts tied to stable identities and analysis contexts. Reusing a name
like `x` across different values is not a way to transfer a proof. Chapter 24
explains how an actual compiler coordinates facts, installation and invalidation.

[Previous](18-command-line-and-diagnostics.md) · [Contents](README.md) · [Next: Native runtime](20-native-runtime-and-modules.md)
