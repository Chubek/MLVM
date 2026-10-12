// Extent is proven empty; API preconditions and argument evaluations still matter.
// Region descriptor retains state/result, while the proof discharges other operands.
Kernel ZeroLengthMemoryCopy;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::{EmptyExtent, EmptyMemoryCopySafe};
DropEmptyCopy :: Binary(Term!resolves, Term!resolves) -> Term
where EmptyExtent($2) && EmptyMemoryCopySafe { $1 `memory_copy` $2 ~> $1; }
