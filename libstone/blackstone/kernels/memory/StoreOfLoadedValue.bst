// store(address, load(address, state)) is redundant only without intervening changes,
// races/volatile/atomic access, traps, and observable address evaluation changes.
Kernel StoreOfLoadedValue;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::RedundantStoreSafe;
DropStoreBack :: Binary(Term!resolves, Binary(Term!same($1), Term!resolves)) -> Term
where RedundantStoreSafe { $1 `store` ($2 `load` $3) ~> $3; }
