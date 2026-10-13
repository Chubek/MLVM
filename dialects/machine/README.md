# Machine-facing dialect

`machine` implements typed [integer expressions](../README.md) and
`machine.copy`, whose operand and result types must agree. SSA values remain
virtual values until target emission; they are not physical register numbers.

The x86-64 expression emitter assigns i32 values stack slots and emits SysV
parameter transfers, arithmetic, signed division and returns into an ELF64
object. Its current limit is six integer parameters per function. Register
encodings are explicit in the emitter; target lookup never guesses them.

For the independent instruction encoder/decoder API, see `mlvm/codegen.hpp`.
