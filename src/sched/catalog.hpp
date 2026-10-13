#pragma once
#include <mlvm/blackstone.hpp>
#include <sstream>
#include <string>

namespace mlvm::sched::detail {
// Blackstone's published descriptor contracts (python/blackstone/catalog.py).
// Registration supplies vocabulary, never a proof of a host transformation.
inline bool register_catalog(BSRegistry* registry, BSError& error) {
    struct Domain { const char *module, *domain, *term, *binary, *operators, *proofs, *facts; };
    const Domain domains[]{
        {"Statements", "statement", "Statement", "Composition",
         "seq discard assign declare scope cleanup copy_statement substitute_uses",
         "SequenceIdentitySafe SequenceAssociative CanonicalStatementOrder IndependentStatements DeadAssignmentSafe UnusedDeclarationSafe ScopeElisionSafe UnreachableSuffixSafe DuplicateAssertSafe RedundantCleanupSafe CopyPropagationSafe StatementDiscardSafe", "NoOp NoNormalExit"},
        {"Memory", "memory", "State", "Operation",
         "load_after_store store load paired_load seq_store memory_copy memory_move seq_memory fill extract narrow_load narrow_slice",
         "StoreLoadForwardingSafe RedundantLoadSafe DeadStoreSafe RedundantStoreSafe EmptyMemoryCopySafe NonOverlappingCopySafe DuplicateFillSafe NarrowLoadSafe", "EmptyExtent"},
        {"ControlFlow", "control", "Region", "Operation",
         "branch targets jump_true jump_false switch jump_default branch_case block dead_block blocks merged_block threaded_branch diamond predicated_select exits shared_return edge split_edge phi phi_value",
         "BranchFoldSafe BranchConditionDiscardSafe EmptySwitchSafe SingleCaseSwitchSafe UnreachableBlockSafe LinearMergeSafe JumpThreadingSafe IfConversionSafe ReturnMergeSafe CriticalEdgeSafe UniformPhiSafe", "ConditionTrue ConditionFalse EmptyCases"},
        {"Loops", "loop", "Descriptor", "Operation",
         "loop loop_exit inline_iteration hoisted_loop sunk_loop unswitched_loop peeled_loop unrolled_loop loop_pair fused_loop paired_domain seq_loop_body privatized_reduction vector_loop",
         "ZeroTripSafe SingleTripSafe InvariantHoistSafe InvariantSinkSafe UnswitchSafe PeelSafe UnrollSafe LoopFusionSafe LoopFissionSafe ReductionPrivatizationSafe VectorizationSafe DeadLoopSafe", ""},
        {"Polyhedra", "polyhedral", "Descriptor", "Operation",
         "scop interchanged_scop tiled_scop skewed_scop scop_pair fused_scop union_domain fused_schedule distributed_scop parallel_scop coalesced_domain empty_scop",
         "InterchangeSafe TilingSafe SkewingSafe ScheduleFusionSafe DistributionSafe ParallelScheduleSafe DomainCoalescingSafe EmptyScopSafe", "EmptyDomain"}
    };
    for (const auto& domain : domains) {
        for (auto [name, sort] : {std::pair{domain.term, BS_TERM}, {domain.binary, BS_BINARY}}) {
            auto qualified = std::string("HIR::") + domain.module + "::" + name;
            if (bs_concern_find(registry, qualified.c_str())) continue;
            BSConcern view{qualified.c_str(), sort}; BSConcernModule module{domain.module, &view, 1, nullptr, 0};
            if (bs_concern_load(registry, &module, &error) != BS_OK) return false;
        }
        std::istringstream operators(domain.operators); std::string name;
        while (operators >> name) {
            if (bs_operator_find(registry, name.c_str())) continue;
            BSOperator op{name.c_str(), domain.domain, 0, 0}; BSConcernModule module{domain.module, nullptr, 0, &op, 1};
            if (bs_concern_load(registry, &module, &error) != BS_OK) return false;
        }
        for (auto [names, arity] : {std::pair{domain.proofs, 0u}, {domain.facts, 1u}}) {
            std::istringstream capabilities(names);
            while (capabilities >> name) {
                auto qualified = std::string("Optimization::") + domain.module + "::" + name;
                if (bs_requires_find(registry, qualified.c_str())) continue;
                BSCapability capability{qualified.c_str(), name.c_str(), arity, arity ? "" : domain.domain};
                BSRequiresModule module{domain.module, &capability, 1};
                if (bs_requires_load(registry, &module, &error) != BS_OK) return false;
            }
        }
    }
    return true;
}
} // namespace mlvm::sched::detail
