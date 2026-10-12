// Replacing extract(load, slice) requires endian/offset/type/extent agreement.
// Proof preserves faults, alignment, access count and volatile/atomic restrictions.
Kernel NarrowLoadFolding;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::NarrowLoadSafe;
FoldLoadSlice :: Binary(Binary(Term!resolves, Term!resolves), Term!resolves) -> Binary
where NarrowLoadSafe { ($1 `load` $2) `extract` $3 ~> $1 `narrow_load` ($2 `narrow_slice` $3); }
