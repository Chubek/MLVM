// No normal, exceptional, indirect, externally visible or deoptimization entry.
// dead_block is a deletion descriptor; installation repairs CFG/debug references.
Kernel UnreachableBlockElimination;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::UnreachableBlockSafe;
MarkUnreachableBlock :: Binary(Term!resolves, Term!resolves) -> Binary
where UnreachableBlockSafe { $1 `block` $2 ~> $1 `dead_block` $2; }
