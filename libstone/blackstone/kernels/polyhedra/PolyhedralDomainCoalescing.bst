// Coalesced integer set is exactly equal under parameter assumptions, not just a
// convex overapproximation. Statement tags/multiplicity and schedule remain intact.
Kernel PolyhedralDomainCoalescing;
concerns HIR::Polyhedra::Descriptor;
concerns HIR::Polyhedra::Operation;
requires Optimization::Polyhedra::DomainCoalescingSafe;
CoalesceEquivalentIntegerDomain :: Binary(Binary(Term!resolves, Term!resolves), Term!resolves) -> Binary
where DomainCoalescingSafe { ($1 `union_domain` $2) `scop` $3 ~> ($1 `coalesced_domain` $2) `scop` $3; }
