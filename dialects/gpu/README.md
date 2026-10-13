# GPU kernel dialect

`gpu` implements the [common integer expressions](../README.md), plus:

* `gpu.thread_id`: no operands, one i32 result, `axis=x|y|z`.
* `gpu.barrier`: no operands or results; an observable synchronization effect.
* `gpu.launch`: at least six operands, no result, nonempty `callee`. The first
  six operands are `index` values for grid x/y/z and block x/y/z; remaining
  operands are kernel arguments.

Launch and barrier effects are unknown to generic DCE. Host scheduler actions
can supply a device runtime and consume these verified operations. Standard
lowering reaches GPU integer expressions; the built-in CPU/Wasm/LLVM expression
targets reject device-specific operations rather than implying a device backend.
