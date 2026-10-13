# Interp Dialect

`interp` supplies the [common integer expressions](../README.md) and explicit
runtime boundaries:

* `interp.call`: arbitrary arguments, zero or one result, nonempty `callee`.
* `interp.guard`: one i1 condition, no result, nonempty `deopt` continuation.

Calls and guards have unknown effects and cannot be erased as pure expressions.
The `interp-unknown-unknown` target evaluates one integer expression function
with wrapping i1..i64 arithmetic, selection and copies. Signed division reports
zero-divisor and signed-minimum/-1 traps. Calls and deoptimization require a
host runtime action; the baseline interpreter diagnoses them.

Scheduler artifacts can also carry Firestone SSA or 3AC for host JIT/AOT actions.
