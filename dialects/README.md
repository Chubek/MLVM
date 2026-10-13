# Implemented dialects

`register_builtin_dialects` registers every dialect below. The definitions live
in `src/dialect/{builtin,c,dialects}.cpp`; `include/mlvm/dialect.hpp` provides
standard conversion routes. Repeated registration is idempotent.

| Directory | Operations and purpose |
| --- | --- |
| [builtin](builtin/README.md) | Functions, arithmetic, index values, memory, assertions |
| [c](c/README.md) | Existing typed C and structured control-flow implementation |
| [high](high/README.md) | Integer expressions and structured `if`/`yield` |
| [mid](mid/README.md) | Canonical SSA integer expressions and selection |
| [low](low/README.md) | Integer expressions, width casts, explicit memory |
| [machine](machine/README.md) | Target-facing integer expressions and copies |
| [interp](interp/README.md) | Interpreter expressions, calls and deoptimization guards |
| [llvmir](llvmir/README.md) | LLVM-oriented integer expressions and memory |
| [wasm](wasm/README.md) | WebAssembly i32/i64 expressions and drop |
| [gpu](gpu/README.md) | Kernel arithmetic, thread identity, launches and barriers |

The common expression vocabulary is `constant`, `add`, `sub`, `mul`, `div`,
`and`, `or`, `xor`, `select`. Binary operands and result have identical i1..i64
types; `constant` has a representable decimal `value`; `div` is signed and
potentially trapping. Selection requires matching value types and an i1
condition (WebAssembly uses i32). Effects are explicit and division is not
classified as removable pure computation in these dialects.

Standard conversions lower the common expression vocabulary through
`arith -> high -> mid -> low -> {machine,interp,llvmir,wasm,gpu}`. Builtin
function containers and returns remain legal. A route does not invent lowering
for memory, structured control, launch or deoptimization operations. Verification
or missing-route failure leaves the input module intact.

The scheduler's `convert` action exposes these routes. The `target` action uses
the [implemented expression emitters](../targets/README.md).
