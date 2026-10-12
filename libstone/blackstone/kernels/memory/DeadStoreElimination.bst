// The second store fully overwrites the first before any possible read/observer.
// Removing the first address/value evaluations must also preserve their effects.
Kernel DeadStoreElimination;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::DeadStoreSafe;
DropOverwrittenStore :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!same($1), Term!resolves)) -> Binary
where DeadStoreSafe { ($1 `store` $2) `seq_store` ($3 `store` $4) ~> $3 `store` $4; }
