# Dialect implementation

`builtin.cpp` registers foundational functions and arithmetic, `c.cpp` implements
typed C and its cross-operation verifier, and `dialects.cpp` implements the
standard high/mid/low/machine/interp/llvmir/wasm/gpu operation contracts and
expression conversion graph. Public entry points are in `mlvm/core.hpp`,
`mlvm/c.hpp`, and `mlvm/dialect.hpp`. See the [inventory](../../dialects/README.md).
