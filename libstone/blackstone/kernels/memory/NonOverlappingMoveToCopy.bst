// Region descriptor packages source/destination; proof covers full byte extents,
// pointer provenance, object validity and the host move/copy API contracts.
Kernel NonOverlappingMoveToCopy;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::NonOverlappingCopySafe;
SpecializeDisjointMove :: Binary(Term!resolves, Term!resolves) -> Binary
where NonOverlappingCopySafe { $1 `memory_move` $2 ~> $1 `memory_copy` $2; }
