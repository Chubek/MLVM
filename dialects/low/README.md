# Low-level dialect

`low` implements the [common integer expressions](../README.md), plus `low.load`
and `low.store` with explicit `ptr` addresses and checked power-of-two alignment.
Loads have read effects; stores have write effects.

`low.cast` converts one integer value to another width. Its required `kind`
attribute is `trunc` for decreasing width, or `sext`/`zext` for increasing width.
Equal widths and mismatched kinds are rejected.

Common expressions lower to `machine`, `interp`, `llvmir`, `wasm` or `gpu`.
Memory and width casts require target-specific lowering rather than operation
renaming. Target metadata supplies explicit ABI and pointer-width information.
