# Chapter 23 — Testing and correctness

Blackstone verification combines source diagnostics, rule execution, independent
integer evaluation, effect/identity counterexamples and host API contract checks.
Passing static validation establishes a well-formed rule; semantic equivalence
and convergence need their own arguments and evidence.

## Run the supplied checks

```sh
ctest --test-dir build --output-on-failure
python3 examples/run_examples.py --engine both
python3 -m blackstone check kernels
python3 -m blackstone check examples/kernels
```

CTest runs the direct native API checks, standalone native example, documented
example collection and Python contract/catalog/native-engine suites. The manifest
runner checks exact types, rule ordering, immutable inputs, explicit nonmatches,
selected origin metadata and identity-preserving fixed points.

## Integer semantics

The semantic contract exercises every acceptance rule through the real matcher
and driver. It compares exhaustive small signed/unsigned widths against an
independent occurrence evaluator, including wrapping arithmetic, bitwise
representations and truncating division. All literal pairs for the seven integer
operators are covered, including exceptional division and signed-one-bit values.

The native bridge runs the same contracts. Wide-integer cases establish that the
native scheduling/matching path does not narrow the host's arithmetic values.
Check the exact output type as well as a printed numeric result; equal numbers
at different widths are not sufficient evidence.

## Proof counterexamples

For erasure, cancellation or movement rules, consider:

* Effectful and state-dependent evaluations.
* Potential traps, divergence, poison and undef.
* Unknown facts, even when true facts would permit the rule.
* Identical-looking, identity-distinct inputs and expression nodes.
* Mixed types, unsupported domains and altered flags on every exposed operator.
* Evaluation count and observable order among retained nonliteral occurrences.

The example collection supplies concrete manifestations of many of these cases.
The factoring example's common operand is pure and total; the two effectful
operands remain in their original observable order. A test should verify the
proof gate as well as the favorable algebraic result.

## Diagnostics and convergence

Parser/resolver tests cover multiline syntax, comments, aliases, imports,
delimiters, unknown names, invalid captures, forward `!same`, tree mismatch,
duplicate names and inheritance cycles. Fixed-point tests cover folding after
reassociation, wrap-to-identity cleanup, deterministic traces and idempotence.
The swap example gives a reproducible budget-exhaustion trace.

Direct C tests also exercise module rollback, unchecked IR, malformed adapter
results and profile round-trips. Every truncated prefix and single-byte mutation
of a sample profile is fed to the reader, checking that failures publish no
partial profile.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --target blackstone-native-test --parallel
./build-sanitize/blackstone-native-test
```

These checks verify the native implementation's memory and undefined-behavior
contracts. A compiler integration additionally validates its own analyses and
installation invariants. The next chapter describes how to make those boundaries
explicit and testable.

[Previous](22-structured-host-kernels.md) · [Contents](README.md) · [Next: Compiler integration](24-compiler-integration-and-extension.md)
