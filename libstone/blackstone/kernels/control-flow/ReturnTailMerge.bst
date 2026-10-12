// Equivalent return bundles include result type/value, cleanup, ABI, lifetime,
// exception/deoptimization state and absence of distinguishing side effects.
Kernel ReturnTailMerge;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::ReturnMergeSafe;
ShareEquivalentReturn :: Binary(Term!resolves, Term!resolves) -> Binary
where ReturnMergeSafe { $1 `exits` $2 ~> $1 `shared_return` $2; }
