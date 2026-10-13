# Typed C dialect

Implemented by `src/dialect/c.cpp` and exposed by `mlvm/c.hpp`. Registration
includes C functions/declarations, locals, globals, memory, casts, calls,
structured branches/loops, returns and arithmetic. `verify_c_module` validates
cross-operation signatures, local ownership and control transfers.

The Cephyr frontend produces these operations; `emit_c_x86_64` performs their
existing SysV AMD64 lowering. Scheduler `ir` and `verify` actions accept C-dialect
modules. The generic expression-to-Firestone bridge diagnoses C operations that
need C-specific lowering rather than treating them as modular arithmetic.

See [Cephyr semantics](../../manual/37-cephyr-codegen.md).
