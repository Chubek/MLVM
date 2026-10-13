# 39. Testing and troubleshooting

[Contents](README.md) · [Next: Extending MLVM](40-extending.md)

Configure with tests enabled, build, and inspect registered tests before choosing
a focused subset:

```sh
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'mlvm-(core|pass|conversion)' --output-on-failure
```

Optional-component tests exist only when their dependencies and targets are
enabled. Python integration tests are registered when a suitable interpreter is
found. Native execution checks are limited to supported Linux/x86-64 hosts.

## Where evidence lives

| Area | Test source |
| --- | --- |
| Contexts and SSA | `tests/core/` |
| Pass rollback and analyses | `tests/pass/pass.cpp` |
| Conversion routes | `tests/conversion/conversion.cpp` |
| Objects and relocation edges | `tests/object.cpp` |
| Broader binary models | `tests/binutils.cpp` |
| Tool status and end-to-end behavior | `tests/integration/toolchain.py` |
| Library-backed CLI | `tests/integration/binutils.py` |
| Scheduler, Firestone, rollback and dialects | `tests/sched.cpp` |
| Native Blackstone rules, integer semantics and catalog | `tests/blackstone_sched.cpp` |
| Emitted native/LLVM/Wasm execution and CLI | `tests/integration/sched.py` |

Run the native kernel and scheduler checks with:

```sh
ctest --test-dir build -R 'mlvm-(sched|blackstone-sched)' --output-on-failure
```

The native test resolves every supplied `.bst` catalog entry and exercises
all 16 algebra rules plus exhaustive small-width signed/unsigned arithmetic.
The CLI test independently executes generated code using cc/Clang/Node when
available. Legacy Optilisp catalog tooling is registered only if its old sources
are present.

## Troubleshooting guide

| Symptom | First check |
| --- | --- |
| Optional tool/target absent | CMake's resolved feature settings and dependency paths |
| Unknown operation | Dialect registration and operation spelling |
| Stale use list | Direct operand edits without `rebuild_uses()` |
| Kernel no-op | Pattern, stage, facts, and candidate conditions |
| Kernel execution error | Supported verb/contracts and selected registry |
| Installed catalog not found | Kernel paths relative to the `.sched` file |
| Undefined link symbol | Symbol definition, archive order, and strong/weak binding |
| Object rejected | Relocatable format subset and enabled binary-tool mode |

For regressions, capture the smallest input, exact command, version, build
features, and diagnostics. Canonical IR and deterministic objects make useful
reproduction artifacts. Add negative cases for malformed input and verify
rollback after partial edits, not just successful transformation output.

Reference: [test registration](../CMakeLists.txt), [scheduler reference](41-schedulers.md).
