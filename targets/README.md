# Version-1 targets and emitters

`TargetRegistry::builtins()` registers explicit target descriptions with schema
version, triple, object machine, pointer width, byte order, ABI, feature set,
target-facing dialects, and assembler/emitter capabilities. `verify_target`
validates descriptions. Registration is not a claim of an external backend.

| Triple | ABI / format | Implementation |
| --- | --- | --- |
| `x86_64-unknown-linux-gnu` | SysV AMD64 / ELF64 | i32 expression functions, six parameters, stack-slot allocation, arithmetic and returns |
| `wasm32-unknown-unknown` | Wasm / binary module | i32/i64 arithmetic, locals, selection, returns and exports |
| `llvmir-unknown-unknown` | consumer-defined / LLVM text | typed integer SSA expression functions |
| `interp-unknown-unknown` | MLVM values / no object | wrapping integer evaluator with checked signed division |

APIs: `include/mlvm/target.hpp`, metadata in `include/mlvm/object.hpp`.
Implementations: `src/target/target.cpp` and `src/target/emit.cpp`. The existing
C-dialect x86 emitter and restricted x86 assembler remain available separately.
Only the x86 target advertises assembler support.

Scheduler `target` actions invoke these implementations. Unsupported types,
operations, control flow or semantic attributes fail explicitly. Target-free
LLVM output leaves layout decisions to its consumer. GPU launch and interpreter
runtime operations require registered host scheduler actions.

See [target examples](../examples/sched/targets.sched) and
[the scheduler reference](../manual/41-schedulers.md).
