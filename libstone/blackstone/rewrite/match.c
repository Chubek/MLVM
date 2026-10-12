#include "internal.h"
#include "vm.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>

void bs_match_dispose(BSMatch *match) {
    free(match->captures); free(match->skeleton); *match = (BSMatch){0};
}

static BSStatus match_node(const BSCompiledRule *rule, size_t index, const BSAdapter *a,
                            BSValue value, BSMatch *match, int *matched, BSError *error) {
    const BSMatchNode *n = &rule->rule.match[index];
    BSValueView v;
    BSStatus status = bs_inspect(a, value, &v, error);
    if (status != BS_OK) return status;
    if (!v.resolved) { *matched = 0; return BS_OK; }
    if (n->sort == BS_BINARY) {
        if (v.kind != BS_BINARY_VALUE || strcmp(v.op, n->text)) { *matched = 0; return BS_OK; }
        match->skeleton[match->skeleton_count++] = value;
        status = match_node(rule, n->left, a, v.left, match, matched, error);
        if (status == BS_OK && *matched) status = match_node(rule, n->right, a, v.right, match, matched, error);
        return status;
    }
    match->captures[match->capture_count++] = value;
    switch (n->refinement) {
    case BS_RESOLVES: break;
    case BS_LITERAL: *matched = v.kind == BS_LITERAL_VALUE; break;
    case BS_NONLITERAL: *matched = v.kind != BS_LITERAL_VALUE; break;
    case BS_EXACT: *matched = v.kind == BS_LITERAL_VALUE && bs_decimal_equal(v.integer, n->text); break;
    case BS_SAME: {
        BSValue previous = match->captures[n->same - 1];
        BSValueView other;
        status = bs_inspect(a, previous, &other, error);
        if (status == BS_OK) *matched = bs_type_equal(&v, &other) &&
            (a->identity ? a->identity(a->context, value, previous) : value == previous);
        break;
    }
    default: *matched = 0; break;
    }
    return status;
}

BSStatus bs_match_rule(const BSCompiledRule *rule, const BSAdapter *adapter, BSValue root,
                       BSMatch *match, int *matched, BSError *error) {
    *match = (BSMatch){0}; *matched = 1;
    match->captures = calloc(rule->captures, sizeof(BSValue));
    match->skeleton = calloc(rule->binaries, sizeof(BSValue));
    if (!match->captures || (rule->binaries && !match->skeleton)) {
        bs_match_dispose(match);
        return bs_error(error, BS_NOMEM, rule->rule.span, "cannot allocate match captures");
    }
    BSStatus status = match_node(rule, rule->rule.match_count - 1, adapter, root, match, matched, error);
    if (status != BS_OK || !*matched) bs_match_dispose(match);
    return status;
}

static BSStatus replacement_check(const BSAdapter *a, BSValue source, BSValue result,
                                   BSSort sort, BSSpan span, BSError *error) {
    BSValueView before, after;
    BSStatus status = bs_inspect(a, source, &before, error);
    if (status == BS_OK) status = bs_inspect(a, result, &after, error);
    if (status != BS_OK) return status;
    if (!after.resolved || !bs_type_equal(&before, &after) || (sort == BS_BINARY && after.kind != BS_BINARY_VALUE))
        return bs_error(error, BS_ADAPTER_ERROR, span, "adapter violated replacement type/sort contract");
    return BS_OK;
}

BSStatus bs_rule_candidate(const BSProfile *profile, const BSCompiledRule *compiled,
                           const BSAdapter *a, BSValue root, BSValue *out, BSError *error) {
    const BSRule *r = &compiled->rule;
    BSValueView view;
    *out = 0;
    BSStatus status = bs_inspect(a, root, &view, error);
    if (status != BS_OK) return status;
    if (compiled->folding) {
        if (view.kind != BS_BINARY_VALUE || !a->fold) return BS_OK;
        int eligible;
        status = bs_concern_apply(profile->registry, a, root, &root, 1, "integer", &eligible, error);
        if (status != BS_OK || !eligible) return status;
        BSValueView left, right;
        if ((status = bs_inspect(a, view.left, &left, error)) != BS_OK ||
            (status = bs_inspect(a, view.right, &right, error)) != BS_OK) return status;
        if (left.kind != BS_LITERAL_VALUE || right.kind != BS_LITERAL_VALUE) return BS_OK;
        *out = a->fold(a->context, root, r->name);
        if (!*out) return BS_OK;
        status = replacement_check(a, root, *out, BS_TERM, r->span, error);
        BSValueView folded;
        if (status == BS_OK) status = bs_inspect(a, *out, &folded, error);
        if (status == BS_OK && folded.kind != BS_LITERAL_VALUE)
            status = bs_error(error, BS_ADAPTER_ERROR, r->span, "constant folder must construct a literal");
        return status;
    }
    BSMatch match;
    int matched;
    status = bs_match_rule(compiled, a, root, &match, &matched, error);
    if (status != BS_OK || !matched) return status;
    /* Host carrier eligibility precedes proof queries, even for forwarding. */
    if (compiled->binaries) {
        const BSMatchNode *m = &r->match[r->match_count - 1];
        const BSOperator *op = bs_operator_find(profile->registry, m->text);
        if (strcmp(op->domain, "integer")) {
            int eligible;
            status = bs_concern_apply(profile->registry, a, root, match.skeleton, match.skeleton_count, op->domain, &eligible, error);
            if (status != BS_OK || !eligible) goto done;
        }
    }
    for (size_t i = 0; i < r->guard_count; ++i) {
        const BSGuard *guard = &r->guards[i];
        BSValue *args = calloc(guard->count, sizeof(*args));
        if (guard->count && !args) { status = bs_error(error, BS_NOMEM, guard->span, "cannot allocate guard arguments"); goto done; }
        for (size_t j = 0; j < guard->count; ++j) args[j] = match.captures[guard->arguments[j] - 1];
        BSFact fact;
        status = bs_requires_apply(compiled->predicates[i], profile->registry, a, root,
                                    args, guard->count, match.skeleton, match.skeleton_count, &fact, error);
        free(args);
        if (status != BS_OK || fact != BS_TRUE) goto done;
    }
    BSValue *values = calloc(r->build_count, sizeof(*values));
    if (!values) { status = bs_error(error, BS_NOMEM, r->span, "cannot allocate replacement values"); goto done; }
    for (size_t i = 0; i < r->build_count; ++i) {
        const BSBuildNode *b = &r->build[i];
        if (b->kind == BS_CAPTURE) values[i] = match.captures[b->capture - 1];
        else if (b->kind == BS_INTEGER) values[i] = a->literal ? a->literal(a->context, b->text, root, r->name) : 0;
        else {
            int eligible;
            const BSOperator *op = bs_operator_find(profile->registry, b->text);
            status = bs_concern_apply(profile->registry, a, root, match.skeleton, match.skeleton_count, op->domain, &eligible, error);
            if (status != BS_OK || !eligible) break;
            values[i] = a->binary ? a->binary(a->context, b->text, values[b->left], values[b->right], root, r->name) : 0;
        }
        if (!values[i]) break;
        /* Captures with a different type simply make construction ineligible. */
        BSValueView built;
        status = bs_inspect(a, values[i], &built, error);
        if (status != BS_OK) break;
        if (b->kind == BS_CAPTURE && !bs_type_equal(&view, &built)) { values[i] = 0; break; }
        status = replacement_check(a, root, values[i], b->kind == BS_BUILD_BINARY ? BS_BINARY : BS_TERM, b->span, error);
        if (status != BS_OK) break;
        if (b->kind == BS_INTEGER && (built.kind != BS_LITERAL_VALUE || !bs_decimal_equal(built.integer, b->text))) {
            status = bs_error(error, BS_ADAPTER_ERROR, b->span, "adapter violated integer literal construction contract"); break;
        }
        if (b->kind == BS_BUILD_BINARY && (strcmp(built.op, b->text) || built.flag_count ||
                                           built.left != values[b->left] || built.right != values[b->right])) {
            status = bs_error(error, BS_ADAPTER_ERROR, b->span, "adapter violated binary construction contract"); break;
        }
    }
    if (status == BS_OK) *out = values[r->build_count - 1];
    free(values);
done:
    bs_match_dispose(&match);
    return status;
}
