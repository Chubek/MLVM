# High-level dialect

`high` supplies the [common integer expressions](../README.md), plus:

* `high.if(condition)`: i1 condition, two regions, zero or more typed results.
  Each arm has one nonempty block without arguments and ends with `high.yield`.
* `high.yield(values...)`: a terminator. An enclosing `high.if` checks that
  every yielded value agrees with the corresponding result type.

Both structured operations have unknown effects. Expression lowering converts
`high` to `mid`; structured `if` requires an explicit host control-flow lowering.
