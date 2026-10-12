#include "internal.h"
#include "vm.h"
#include <string.h>

typedef struct {
    const BSProfile *profile;
    const BSAdapter *adapter;
    BSRewriteOptions options;
    BSRewriteStats stats;
    BSMap memo;
    const char *recent[8];
    BSError *error;
} Run;

static BSStatus visit(Run *run, BSValue original, size_t depth, BSValue *out) {
    if (depth > run->options.max_depth)
        return bs_error(run->error, BS_NONCONVERGENCE, (BSSpan){0}, "expression depth limit %zu exceeded after %zu rewrites", run->options.max_depth, run->stats.rewrites);
    BSMapEntry *entry = bs_map_get(&run->memo, original, 1);
    if (!entry) return bs_error(run->error, BS_NOMEM, (BSSpan){0}, "cannot allocate rewrite memo");
    if (entry->state == 1) return bs_error(run->error, BS_INVALID, (BSSpan){0}, "cyclic expression input");
    if (entry->state == 2) { *out = entry->value; return BS_OK; }
    entry->state = 1; ++run->stats.visited;
    BSValue node = original;
    const BSAdapter *a = run->adapter;
    for (;;) {
        BSValueView view;
        BSStatus status = bs_inspect(a, node, &view, run->error);
        if (status != BS_OK) return status;
        if (view.kind == BS_BINARY_VALUE) {
            BSValue left, right;
            status = visit(run, view.left, depth + 1, &left);
            if (status == BS_OK) status = visit(run, view.right, depth + 1, &right);
            if (status != BS_OK) return status;
            if (left != view.left || right != view.right) {
                BSValue rebuilt = a->rebuild ? a->rebuild(a->context, node, left, right) : 0;
                BSValueView after;
                if (!rebuilt) return bs_error(run->error, BS_ADAPTER_ERROR, (BSSpan){0}, "adapter could not rebuild normalized operands");
                status = bs_inspect(a, rebuilt, &after, run->error);
                if (status != BS_OK) return status;
                if (after.kind != BS_BINARY_VALUE || !bs_type_equal(&view, &after) ||
                    strcmp(view.op, after.op) || view.resolved != after.resolved ||
                    view.flag_count != after.flag_count || after.left != left || after.right != right)
                    return bs_error(run->error, BS_ADAPTER_ERROR, (BSSpan){0}, "adapter violated operand rebuild contract");
                node = rebuilt;
            }
        }
        BSValue replacement = 0;
        for (size_t i = 0; i < run->profile->count; ++i) {
            const BSCompiledRule *rule = &run->profile->rules[i];
            status = bs_rule_candidate(run->profile, rule, a, node, &replacement, run->error);
            if (status != BS_OK) return status;
            if (!replacement || replacement == node || (a->equivalent && a->equivalent(a->context, node, replacement))) {
                replacement = 0; continue;
            }
            if (run->stats.rewrites >= run->options.budget) {
                char recent[512] = "";
                size_t first = run->stats.rewrites > 8 ? run->stats.rewrites - 8 : 0;
                for (size_t j = first; j < run->stats.rewrites; ++j) {
                    size_t used = strlen(recent);
                    snprintf(recent + used, sizeof(recent) - used, "%s%s", j == first ? "" : ", ", run->recent[j % 8]);
                }
                return bs_error(run->error, BS_NONCONVERGENCE, rule->rule.span,
                                "rewrite budget %zu exhausted before %s; last rules: %s",
                                run->options.budget, rule->rule.name, recent);
            }
            run->recent[run->stats.rewrites % 8] = rule->rule.name;
            ++run->stats.rewrites;
            if (a->trace) {
                BSTrace trace = {rule->rule.name, rule->rule.span, node, replacement, run->stats.rewrites};
                a->trace(a->context, &trace);
            }
            break;
        }
        if (!replacement) {
            /* Recursive insertions may have moved the hash table. */
            entry = bs_map_get(&run->memo, original, 0);
            entry->state = 2; entry->value = node;
            *out = node; return BS_OK;
        }
        node = replacement;
    }
}

BSStatus bs_optimize(const BSProfile *profile, const BSAdapter *adapter, BSValue root,
                     const BSRewriteOptions *options, BSValue *out, BSRewriteStats *stats, BSError *error) {
    if (out) *out = 0;
    if (stats) *stats = (BSRewriteStats){0};
    if (!profile || !adapter || adapter->abi_version != BS_ABI_VERSION || !adapter->inspect || !root || !out)
        return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid profile, adapter ABI, or rewrite input");
    Run run = {0}; run.profile = profile; run.adapter = adapter; run.error = error;
    run.options = options ? *options : (BSRewriteOptions){10000, BS_MAX_DEPTH};
    if (!run.options.max_depth || run.options.max_depth > BS_MAX_DEPTH)
        return bs_error(error, BS_INVALID, (BSSpan){0}, "max_depth must be in 1..%u", BS_MAX_DEPTH);
    BSValue result = 0;
    BSStatus status = visit(&run, root, 1, &result);
    bs_map_dispose(&run.memo);
    if (stats) *stats = run.stats;
    if (status == BS_OK) { *out = result; if (error) *error = (BSError){0}; }
    return status;
}
