"""Explicit contracts for the extended kernel catalog.

Host operators use typed descriptor carriers, not executable integer operations.
Their proof queries are unknown in TreeAdapter. A host supplies their semantics,
proofs and construction/installation; registering a name is not a proof.
"""

HOST_OPERATORS = {
    "statement": "seq discard assign declare scope cleanup copy_statement substitute_uses",
    "memory": "load_after_store store load paired_load seq_store memory_copy memory_move seq_memory fill extract narrow_load narrow_slice",
    "control": "branch targets jump_true jump_false switch jump_default branch_case block dead_block blocks merged_block threaded_branch diamond predicated_select exits shared_return edge split_edge phi phi_value",
    "loop": "loop loop_exit inline_iteration hoisted_loop sunk_loop unswitched_loop peeled_loop unrolled_loop loop_pair fused_loop paired_domain seq_loop_body privatized_reduction vector_loop",
    "polyhedral": "scop interchanged_scop tiled_scop skewed_scop scop_pair fused_scop union_domain fused_schedule distributed_scop parallel_scop coalesced_domain empty_scop",
}

# Each zero-argument *Safe query certifies the complete matched transformation,
# including implicit effects, exceptional exits, dependencies and host typing.
HOST_PROOFS = {
    "Statements": "SequenceIdentitySafe SequenceAssociative CanonicalStatementOrder IndependentStatements DeadAssignmentSafe UnusedDeclarationSafe ScopeElisionSafe UnreachableSuffixSafe DuplicateAssertSafe RedundantCleanupSafe CopyPropagationSafe StatementDiscardSafe",
    "Memory": "StoreLoadForwardingSafe RedundantLoadSafe DeadStoreSafe RedundantStoreSafe EmptyMemoryCopySafe NonOverlappingCopySafe DuplicateFillSafe NarrowLoadSafe",
    "ControlFlow": "BranchFoldSafe BranchConditionDiscardSafe EmptySwitchSafe SingleCaseSwitchSafe UnreachableBlockSafe LinearMergeSafe JumpThreadingSafe IfConversionSafe ReturnMergeSafe CriticalEdgeSafe UniformPhiSafe",
    "Loops": "ZeroTripSafe SingleTripSafe InvariantHoistSafe InvariantSinkSafe UnswitchSafe PeelSafe UnrollSafe LoopFusionSafe LoopFissionSafe ReductionPrivatizationSafe VectorizationSafe DeadLoopSafe",
    "Polyhedra": "InterchangeSafe TilingSafe SkewingSafe ScheduleFusionSafe DistributionSafe ParallelScheduleSafe DomainCoalescingSafe EmptyScopSafe",
}

HOST_FACTS = {
    "Statements": "NoOp NoNormalExit",
    "Memory": "EmptyExtent",
    "ControlFlow": "ConditionTrue ConditionFalse EmptyCases",
    "Polyhedra": "EmptyDomain",
}


def register(registry):
    from .resolve import Operator
    for domain, names in HOST_OPERATORS.items():
        for name in names.split():
            registry.operators[name] = Operator(name, domain=domain)
    for module, views in {
        "Statements": ("Statement", "Composition"),
        "Memory": ("State", "Operation"),
        "ControlFlow": ("Region", "Operation"),
        "Loops": ("Descriptor", "Operation"),
        "Polyhedra": ("Descriptor", "Operation"),
    }.items():
        for view, sort in zip(views, ("Term", "Binary")):
            registry.views[f"HIR::{module}::{view}"] = sort
    for module, names in HOST_PROOFS.items():
        for name in names.split():
            registry.capabilities[f"Optimization::{module}::{name}"] = (name, 0)
    for module, names in HOST_FACTS.items():
        for name in names.split():
            registry.capabilities[f"Optimization::{module}::{name}"] = (name, 1)
    for name in ("AllOnes", "Nonzero"):
        registry.capabilities[f"Arithmetic::Values::{name}"] = (name, 1)
