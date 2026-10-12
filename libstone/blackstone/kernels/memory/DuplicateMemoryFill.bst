// Identical region/pattern descriptors; no observer, trap, volatile or atomic write.
// Proof includes effects of evaluating both fill argument occurrences.
Kernel DuplicateMemoryFill;
concerns HIR::Memory::State;
concerns HIR::Memory::Operation;
requires Optimization::Memory::DuplicateFillSafe;
KeepSingleFill :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!same($1), Term!same($2))) -> Binary
where DuplicateFillSafe { ($1 `fill` $2) `seq_memory` ($3 `fill` $4) ~> $1 `fill` $2; }
