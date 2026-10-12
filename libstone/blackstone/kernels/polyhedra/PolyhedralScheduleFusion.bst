// Tagged union retains statement identity even for overlapping iteration domains.
// Fused schedule satisfies every intra/inter-region dependence and parameter guard.
Kernel PolyhedralScheduleFusion;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::ScheduleFusionSafe;
FuseCertifiedScopSchedules :: Binary(Binary(Term!resolves, Term!resolves), Binary(Term!resolves, Term!resolves)) -> Binary
where ScheduleFusionSafe
{ ($1 `scop` $2) `scop_pair` ($3 `scop` $4) ~> ($1 `union_domain` $3) `fused_scop` ($2 `fused_schedule` $4); }
