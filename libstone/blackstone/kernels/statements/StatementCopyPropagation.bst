// Substitute a dominating stable copied value; retain its defining effects.
// Proof covers reaching definitions, aliases, type, lifetime and debug uses.
Kernel StatementCopyPropagation;
concerns HIR::Statements::Statement;
concerns HIR::Statements::Composition;
requires Optimization::Statements::CopyPropagationSafe;
PropagateStatementCopy :: Binary(Term!resolves, Term!resolves) -> Binary
where CopyPropagationSafe { $1 `copy_statement` $2 ~> $1 `substitute_uses` $2; }
