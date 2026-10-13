# How to implement a dialect

A dialect gives meaning to a group of operation names, type spellings,
attributes, effects and verification rules. In MLVM, dialect support is explicit:
register the dialect, register every operation contract, add conversion routes or
targets only when semantics are known, and cover the contract with tests.

Use this guide when adding a new built-in dialect or extending one of the
scaffolded dialect inventories under `dialects/`.

## 1. Write the contract first

Add or update `dialects/<name>/README.md` before coding. Define:

- Operation names, arity and result counts.
- Required attributes and accepted values.
- Type rules for operands, results and regions.
- Effects: `pure`, `read`, `write` or `unknown`.
- Terminator behavior.
- Conversion and target-emission expectations.

Do not rely on a target backend, pass, or parser to infer missing semantics. The
dialect contract is the source of truth.

## 2. Choose the implementation location

Current built-in dialect registration lives in:

- `src/dialect/builtin.cpp` — `builtin`, `func`, `arith` and registration entry.
- `src/dialect/c.cpp` — structured typed C dialect and module-level checks.
- `src/dialect/dialects.cpp` — foundation extensions and standard
  `high`/`mid`/`low`/`machine`/`interp`/`llvmir`/`wasm`/`gpu` dialects.

Public declarations are in:

- `include/mlvm/core.hpp` for `OperationDefinition`, `Context`, `Effect`.
- `include/mlvm/dialect.hpp` for built-in standard dialect/conversion entry points.
- `include/mlvm/c.hpp` for the C dialect.

For a small built-in dialect, add it to `src/dialect/dialects.cpp`. For a larger
one, create `src/dialect/<name>.cpp`, expose a registration function in a public
or internal header, and add the file to `mlvm_ir` in `CMakeLists.txt`.

## 3. Register the dialect and operations

Register the dialect before its operations. Operation names use the
`dialect.operation` form.

```cpp
bool register_demo_dialect(mlvm::Context& context, mlvm::DiagnosticEngine& diagnostics) {
    bool ok = context.has_dialect("demo") || context.register_dialect("demo", diagnostics);

    auto add = [&](mlvm::OperationDefinition def) {
        if (!context.operation(def.name))
            ok = context.register_operation(std::move(def), diagnostics) && ok;
    };

    add({"demo.marker", 0, 0, 0, 0, 0, 0, mlvm::Effect::pure, false, {}});
    return ok;
}
```

Registration should be idempotent when used by the built-in registration path.
Duplicate explicit definitions should still fail when they conflict.

## 4. Add operation verifiers

Use `OperationDefinition::verify` for rules that arity alone cannot express:

- Operand/result type compatibility.
- Required attributes and enumerated attribute values.
- Region shape and terminator expectations.
- Symbol, alignment, width, or constant-range checks.

Verifier callbacks receive malformed IR. Always check sizes and lookups before
indexing operands, results, attributes or values.

```cpp
auto verify_binary_i32 = [](const mlvm::Operation& op,
                            const mlvm::Module& module,
                            mlvm::DiagnosticEngine& diagnostics) {
    auto fail = [&](std::string message) {
        diagnostics.error("demo.verify", std::move(message), op.location);
        return false;
    };
    if (op.operands.size() != 2 || op.results.size() != 1)
        return fail("demo.add requires two operands and one result");

    const auto* result = module.value(op.results[0]);
    if (!result || module.context->spelling(result->type) != "i32")
        return fail("demo.add requires an i32 result");

    for (auto id : op.operands) {
        const auto* value = module.value(id);
        if (!value || value->type != result->type)
            return fail("demo.add operands must match the result type");
    }
    return true;
};
```

If a rule needs parent context, consider whether it belongs in generic module
verification (`src/ir/ir.cpp`) or a module-level dialect verifier like
`verify_c_module` in `src/dialect/c.cpp`. Operation callbacks only see the current
operation, not its parent operation.

## 5. Define effects and terminators accurately

Effects are part of the optimization contract:

- `Effect::pure`: removable when unused.
- `Effect::read`: observes memory or external state.
- `Effect::write`: mutates memory or external state.
- `Effect::unknown`: may trap, synchronize, call out, or otherwise be observable.

Mark terminators explicitly. The generic verifier enforces terminators as the last
operation in their block.

Do not mark an operation pure unless it is safe for generic dead-code elimination
to erase it when its results are unused.

## 6. Add conversions only when semantics are preserved

Conversions live in `src/dialect/dialects.cpp` through `standard_conversions()` or
in a custom `ConversionGraph`. A conversion edge rewrites one operation from a
source dialect to a target dialect.

```cpp
graph.add({"demo", "low", [](mlvm::Operation& op,
                              mlvm::Module& module,
                              mlvm::DiagnosticEngine& diagnostics) {
    (void)module;
    if (op.name != "demo.add") {
        diagnostics.error("demo.lower", "operation has no low lowering", op.location);
        return false;
    }
    op.name = "low.add";
    return true;
}});
```

Conversion is transactional: failure, emitted errors or verification errors leave
the input module unchanged. Do not rename operations across dialects unless types,
attributes, effects and runtime behavior remain valid.

## 7. Update target emission only for supported operations

Target helpers such as `emit_llvm_ir`, `emit_wasm`, `emit_x86_object` and
`interpret` live under `src/target/` and are intentionally narrow. If a new dialect
operation can be emitted, update the target and reject unsupported semantic
attributes rather than silently dropping them.

If an operation needs ABI, pointer-width, runtime, device or memory-layout data,
keep it verified at the dialect level but require a target-specific lowering
instead of pretending a generic emitter can handle it.

## 8. Add tests

At minimum, add tests that show:

- The dialect is registered by the intended entry point.
- Valid textual IR parses and verifies.
- Invalid arity, type, attribute, region and effect-sensitive cases fail.
- Conversions preserve behavior and roll back on failure.
- Target emission accepts only supported operations.

Existing dialect coverage is in `tests/sched.cpp`; conversion-specific behavior is
in `tests/conversion/conversion.cpp`. Run:

```sh
cmake -S . -B build -DMLVM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Checklist

- [ ] `dialects/<name>/README.md` defines the operation contract.
- [ ] Dialect registration is idempotent.
- [ ] Every operation has correct arity, effect and terminator metadata.
- [ ] Verifiers safely handle malformed IR.
- [ ] Parent/module-level invariants are checked outside operation callbacks.
- [ ] Conversion routes are semantic-preserving and transactional.
- [ ] Targets either implement or explicitly reject the new operations.
- [ ] Tests cover valid and invalid IR plus conversion/target behavior where applicable.
