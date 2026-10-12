# Runnable Blackstone examples

Run commands from `libstone/blackstone`. Build and select the tools first:

```sh
cmake -S . -B build
cmake --build build --parallel
export BLACKSTONE_PARSER="$PWD/build/blackstone-parser"
export BLACKSTONE_LIBRARY="$PWD/build/libblackstone.so"
export PYTHONPATH="$PWD/python"
```

The examples use only Python's standard library and the supplied native build.
Set `BLACKSTONE_LIBRARY` to the platform-specific library filename when needed.
The [24-chapter manual](../manual/README.md) explains the language and APIs.

## Run and verify the collection

```sh
python3 examples/run_examples.py --engine both
python3 examples/run_examples.py --engine native --case wrapping-cleanup
ctest --test-dir build --output-on-failure
```

`cases.json` is the collection's manifest, not a HIR fixture. Its 27 cases name
their kernel, HIR input, expected result and exact rule sequence. The runner checks
exact result types, input immutability, unchanged-node identity for nonmatches,
selected origin metadata and identity-preserving idempotence. Budget failures are
intentional expected outcomes. Paths in the manifest are relative to Blackstone;
the runner resolves them independently of the current directory.

## HIR fixtures

Unless the table names another kernel, use
`kernels/expressions/AlgebraicSimplification.bst`:

```sh
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/wrapping-cleanup.json --trace --engine native
```

| Fixture | Expected result | Lesson |
| --- | --- | --- |
| [reassociate.json](reassociate.json) | `x plus 7`, signed 8-bit | Reassociation followed by inherited folding |
| [add-zero-right.json](add-zero-right.json) | `effectful_x` | Forwarding preserves one effectful evaluation |
| [constant-right.json](constant-right.json) | `effectful_x plus 3` | Oriented literal movement |
| [multiply-zero.json](multiply-zero.json) | `0` | Pure, total operands may be discarded |
| [effectful-zero.json](effectful-zero.json) | Unchanged | Effects block annihilation |
| [unknown-facts.json](unknown-facts.json) | Unchanged | Unknown is not a proof |
| [trapping-zero.json](trapping-zero.json) | Unchanged | Purity alone does not imply totality |
| [subtract-self.json](subtract-self.json) | Typed `0` | Same identity, pure and total |
| [distinct-values.json](distinct-values.json) | Unchanged | Two nodes named `x` have different identities |
| [shared-normalization.json](shared-normalization.json) | `0` | Shared identity survives child normalization |
| [wrapping-cleanup.json](wrapping-cleanup.json) | `x`, unsigned 3-bit | `3 + 5` wraps to zero, then identity cleanup runs |
| [signed-wrap.json](signed-wrap.json) | `-116`, signed 8-bit | `120 + 20` wraps in the original type |
| [signed-division.json](signed-division.json) | `-2`, signed 8-bit | Division truncates toward zero |
| [divide-by-zero.json](divide-by-zero.json) | Unchanged | Exceptional division remains in HIR |
| [signed-division-overflow.json](signed-division-overflow.json) | Unchanged | Signed minimum divided by `-1` is exceptional |
| [signed-one-bit.json](signed-one-bit.json) | Unchanged | One-bit `-1` is not positive one |
| [nested-overflow-flag.json](nested-overflow-flag.json) | Unchanged | Every exposed operator's flags matter |
| [mixed-types.json](mixed-types.json) | Unchanged | Width mismatches block modular guards |
| [factor-common-right.json](factor-common-right.json) | `(effectful_a plus effectful_b) times common` | Use `kernels/expressions/FactorCommonRight.bst`; the common factor is pure, total and shared |
| [all-ones.json](all-ones.json) | `x` | Use `kernels/expressions/AndAllOnesRight.bst`; an exact-width mask is an identity |
| [wide-integer.json](wide-integer.json) | `0`, unsigned 128-bit | Values are not limited to machine words |
| [loop-descriptor.json](loop-descriptor.json) | Unchanged | Use `kernels/loops/ZeroTripLoopElimination.bst`; an opaque descriptor does not prove `ZeroTripSafe` |
| [swap-input.json](swap-input.json) | Budget exhaustion | Use `examples/kernels/Swap.bst` with a finite budget |

These are metadata fixtures, not executions of the named inputs. The labels
`effectful_x` and `may_trap` are explanatory; the Boolean facts establish the
adapter's contract. Similarly, identical display text in `distinct-values.json`
does not make the two input IDs equal.

## Write and compose kernels

```sh
python3 -m blackstone parse examples/kernels/Identity.bst
python3 -m blackstone check examples/kernels
python3 -m blackstone apply examples/kernels/Reassociate.bst \
  examples/reassociate.json --trace
python3 -m blackstone apply examples/kernels/inheritance/Diamond.bst \
  examples/add-zero-right.json --kernel-dir examples/kernels/inheritance --trace
```

* [Identity.bst](kernels/Identity.bst) is a complete first rule.
* [Reassociate.bst](kernels/Reassociate.bst) demonstrates multiline signatures,
  comments, grouped imports, an alias, `-> Binary`, and inherited folding.
* [inheritance/](kernels/inheritance) demonstrates base-first ordering and a
  shared ancestor. Filenames are conveniences; declared names resolve dependencies.
* [Swap.bst](kernels/Swap.bst) deliberately has no canonical orientation:

```sh
python3 -m blackstone apply examples/kernels/Swap.bst \
  examples/swap-input.json --budget 4 --trace
```

That command exits with status 1, emits four completed swaps and a budget
diagnostic to stderr, and publishes no optimized JSON to stdout.

## Programmatic integration

```sh
python3 examples/programmatic.py --engine python
python3 examples/programmatic.py --engine native
python3 examples/analysis_facts.py --engine python
python3 examples/analysis_facts.py --engine native
./build/blackstone-native-example
```

[programmatic.py](programmatic.py) constructs unsigned-8-bit HIR directly and
checks that reassociation, folding and identity cleanup return the original `x`.
[analysis_facts.py](analysis_facts.py) supplies an explicit nonzero input-domain
proof for a particular stable value, allowing the catalog's `DivideSelf` rule.
Without that proof the expression remains unchanged. Purity and totality remain
separate prerequisites.

[native_profile.c](native_profile.c) uses only public C headers. It compiles a
one-rule profile, writes and reloads it through a temporary stream, then applies
`x + 0 -> x` using immutable host handles and a minimal adapter. CMake builds it
as `blackstone-native-example`; CTest runs it alongside the collection.
