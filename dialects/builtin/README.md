# Foundation dialects

`builtin.func` owns one isolated entry block and requires a unique `sym` and a
`result` type (`void` by default). `func.return` checks that result type.
`arith.constant`, integer arithmetic, division/remainder and bitwise operations
retain their existing i1..i64 verification contracts.

Additional foundations:

* `index.constant`: no operands, one `index` result, unsigned decimal `value`.
* `index.cast`: one operand/result, exactly one `index` and one integer type.
* `mem.load`: one `ptr` address, one result, read effect.
* `mem.store`: `ptr` address and value, no result, write effect.
* Memory `alignment`, when supplied, must be a positive power of two.
* `cf.assert`: one i1 condition, observable assertion/trap effect.
* `cf.unreachable`: operand-free terminator.

Generic SSA verification checks ownership, lexical dominance, use-def chains,
arity and terminator position in addition to these operation-specific contracts.
