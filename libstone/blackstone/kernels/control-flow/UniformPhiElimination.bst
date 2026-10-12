// Every feasible incoming value has the same stable defined SSA identity/type.
// phi_value retains block context for dominance, undef and unreachable-edge checks.
Kernel UniformPhiElimination;
concerns HIR::ControlFlow::Region;
concerns HIR::ControlFlow::Operation;
requires Optimization::ControlFlow::UniformPhiSafe;
ResolveUniformPhi :: Binary(Term!resolves, Term!resolves) -> Binary
where UniformPhiSafe { $1 `phi` $2 ~> $1 `phi_value` $2; }
