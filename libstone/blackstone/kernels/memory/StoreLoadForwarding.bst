// Same address descriptor plus proof of extent/type equality, alignment, dominance,
// alias freedom, defined value, and absence of volatile/atomic observations.
Kernel StoreLoadForwarding;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::StoreLoadForwardingSafe;
ForwardStoredValue :: Binary(Binary(Term!resolves, Term!resolves), Term!same($1)) -> Term
where StoreLoadForwardingSafe { ($1 `store` $2) `load_after_store` $3 ~> $2; }
