// paired_load(previous-value, access) reads the same unchanged valid memory.
// Proof includes dominance, MemorySSA, type/extent, ordering and trapping behavior.
Kernel RedundantLoadReuse;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::RedundantLoadSafe;
ReuseDominatingLoad :: Binary(Term!resolves, Term!resolves) -> Term
where RedundantLoadSafe { $1 `paired_load` $2 ~> $1; }
