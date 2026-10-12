#include "internal.h"
#include "compiler.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>

static const char *short_name(const char *name) {
    const char *last = name, *p;
    while ((p = strstr(last, "::")) != NULL) last = p + 2;
    return last;
}

static int names_overlap(const BSCapability *a, const BSCapability *b) {
    return !strcmp(a->alias, b->alias) || !strcmp(short_name(a->name), b->alias) ||
           !strcmp(a->alias, short_name(b->name)) || !strcmp(short_name(a->name), short_name(b->name));
}

BSStatus bs_declarations_check(const BSRegistry *registry, const BSKernel *kernel, BSError *error) {
    for (size_t i = 0; i < kernel->concern_count; ++i)
        if (!bs_concern_find(registry, kernel->concerns[i].name))
            return bs_error(error, BS_INVALID, kernel->concerns[i].span, "unknown HIR view %s",
                            kernel->concerns[i].name ? kernel->concerns[i].name : "<null>");
    for (size_t i = 0; i < kernel->require_count; ++i) {
        const BSCapability *a = bs_requires_find(registry, kernel->requires[i].name);
        if (!a) return bs_error(error, BS_INVALID, kernel->requires[i].span, "unknown capability %s",
                                kernel->requires[i].name ? kernel->requires[i].name : "<null>");
        for (size_t j = 0; j < i; ++j) {
            const BSCapability *b = bs_requires_find(registry, kernel->requires[j].name);
            if (a != b && names_overlap(a, b))
                return bs_error(error, BS_INVALID, kernel->requires[i].span, "ambiguous imported predicate %s", a->alias);
        }
    }
    return BS_OK;
}

typedef struct {
    const BSRegistry *registry;
    const BSRule *rule;
    unsigned char *seen;
    size_t captures, binaries, visited;
    unsigned sorts;
    const char *domain;
    BSError *error;
} Check;

static BSStatus domain(Check *c, const char *name, BSSpan span) {
    const BSOperator *op = bs_operator_find(c->registry, name);
    if (!op) return bs_error(c->error, BS_INVALID, span, "unknown operator %s", name ? name : "<null>");
    if (c->domain && strcmp(c->domain, op->domain))
        return bs_error(c->error, BS_INVALID, span, "rule cannot mix operator carrier domains");
    c->domain = op->domain;
    return BS_OK;
}

static BSStatus match_check(Check *c, size_t index, size_t depth) {
    const BSMatchNode *n = &c->rule->match[index];
    if (depth > BS_MAX_DEPTH || c->seen[index])
        return bs_error(c->error, BS_INVALID, n->span, "match must be a bounded, unshared tree");
    c->seen[index] = 1; ++c->visited;
    if (!(c->sorts & (1u << n->sort)))
        return bs_error(c->error, BS_INVALID, n->span, "pattern sort requires a concerns declaration");
    if (n->sort == BS_BINARY) {
        ++c->binaries;
        if (n->left >= index || n->right >= index)
            return bs_error(c->error, BS_INVALID, n->span, "binary children must precede their parent");
        BSStatus status = domain(c, n->text, n->span);
        if (status == BS_OK) status = match_check(c, n->left, depth + 1);
        if (status == BS_OK) status = match_check(c, n->right, depth + 1);
        return status;
    }
    ++c->captures;
    if (n->capture != c->captures)
        return bs_error(c->error, BS_INVALID, n->span, "signature/match tree mismatch: expected $%zu", c->captures);
    if (n->refinement < BS_RESOLVES || n->refinement > BS_SAME)
        return bs_error(c->error, BS_INVALID, n->span, "unknown refinement");
    if (n->refinement == BS_SAME && (!n->same || n->same >= n->capture))
        return bs_error(c->error, BS_INVALID, n->span, "!same must reference an earlier one-based capture");
    if (n->refinement == BS_EXACT && !bs_decimal_valid(n->text))
        return bs_error(c->error, BS_INVALID, n->span, "exact refinement requires decimal spelling");
    return BS_OK;
}

static BSStatus build_check(Check *c, size_t index, size_t depth) {
    const BSBuildNode *n = &c->rule->build[index];
    if (depth > BS_MAX_DEPTH || c->seen[index])
        return bs_error(c->error, BS_INVALID, n->span, "replacement must be a bounded, unshared tree");
    c->seen[index] = 1; ++c->visited;
    if (n->kind == BS_CAPTURE) {
        if (!n->capture || n->capture > c->captures)
            return bs_error(c->error, BS_INVALID, n->span, "capture $%u out of range 1..%zu", n->capture, c->captures);
        return BS_OK;
    }
    if (n->kind == BS_INTEGER) {
        if (!bs_decimal_valid(n->text)) return bs_error(c->error, BS_INVALID, n->span, "invalid decimal construction");
        return BS_OK;
    }
    if (n->kind != BS_BUILD_BINARY || n->left >= index || n->right >= index)
        return bs_error(c->error, BS_INVALID, n->span, "invalid replacement instruction or child index");
    BSStatus status = domain(c, n->text, n->span);
    if (status == BS_OK) status = build_check(c, n->left, depth + 1);
    if (status == BS_OK) status = build_check(c, n->right, depth + 1);
    return status;
}

static int copy_span(BSSpan *out, BSSpan in) {
    *out = in; out->path = bs_string(in.path);
    return out->path != NULL;
}

void bs_rule_dispose(BSCompiledRule *compiled) {
    BSRule *r = &compiled->rule;
    free((char *)r->name); free((char *)r->span.path);
    if (r->match) for (size_t i = 0; i < r->match_count; ++i) {
        free((char *)r->match[i].text); free((char *)r->match[i].span.path);
    }
    if (r->build) for (size_t i = 0; i < r->build_count; ++i) {
        free((char *)r->build[i].text); free((char *)r->build[i].span.path);
    }
    if (r->guards) for (size_t i = 0; i < r->guard_count; ++i) {
        free((char *)r->guards[i].name); free((char *)r->guards[i].arguments); free((char *)r->guards[i].span.path);
    }
    if (compiled->predicates) for (size_t i = 0; i < r->guard_count; ++i) {
        const BSCapability *p = compiled->predicates[i];
        if (p) { free((char *)p->name); free((char *)p->alias); free((char *)p->proof_domain); free((void *)p); }
    }
    free((void *)r->match); free((void *)r->build); free((void *)r->guards); free(compiled->predicates);
    *compiled = (BSCompiledRule){0};
}

static BSStatus copy_rule(const BSRule *in, const BSCapability **predicates, BSCompiledRule *out, BSError *error) {
    BSRule *r = &out->rule;
    r->result = in->result;
    r->match_count = in->match_count; r->build_count = in->build_count; r->guard_count = in->guard_count;
    r->name = bs_string(in->name);
    BSMatchNode *match = calloc(r->match_count, sizeof(*match)); r->match = match;
    BSBuildNode *build = calloc(r->build_count, sizeof(*build)); r->build = build;
    BSGuard *guards = calloc(r->guard_count, sizeof(*guards)); r->guards = guards;
    out->predicates = calloc(r->guard_count, sizeof(*out->predicates));
    if (!r->name || !copy_span(&r->span, in->span) || !match || !build ||
        (r->guard_count && (!guards || !out->predicates))) goto nomem;
    for (size_t i = 0; i < r->match_count; ++i) {
        match[i] = in->match[i]; match[i].text = bs_string(in->match[i].text); match[i].span.path = NULL;
        if (!match[i].text || !copy_span(&match[i].span, in->match[i].span)) goto nomem;
    }
    for (size_t i = 0; i < r->build_count; ++i) {
        build[i] = in->build[i]; build[i].text = bs_string(in->build[i].text); build[i].span.path = NULL;
        if (!build[i].text || !copy_span(&build[i].span, in->build[i].span)) goto nomem;
    }
    for (size_t i = 0; i < r->guard_count; ++i) {
        guards[i].count = in->guards[i].count;
        guards[i].name = bs_string(in->guards[i].name);
        uint32_t *args = calloc(guards[i].count, sizeof(*args)); guards[i].arguments = args;
        BSCapability *p = calloc(1, sizeof(*p)); out->predicates[i] = p;
        if (!guards[i].name || !copy_span(&guards[i].span, in->guards[i].span) ||
            (guards[i].count && !args) || !p) goto nomem;
        if (guards[i].count) memcpy(args, in->guards[i].arguments, guards[i].count * sizeof(*args));
        p->name = bs_string(predicates[i]->name); p->alias = bs_string(predicates[i]->alias);
        p->proof_domain = bs_string(predicates[i]->proof_domain); p->arity = predicates[i]->arity;
        if (!p->name || !p->alias || !p->proof_domain) goto nomem;
    }
    return BS_OK;
nomem:
    bs_rule_dispose(out);
    return bs_error(error, BS_NOMEM, in->span, "cannot copy compiled rule");
}

BSStatus bs_rule_compile(const BSRegistry *registry, const BSKernel *kernel,
                          const BSRule *rule, BSCompiledRule *out, BSError *error) {
    *out = (BSCompiledRule){0};
    if (!bs_identifier_valid(rule->name) || (rule->result != BS_TERM && rule->result != BS_BINARY) ||
        !rule->match || !rule->build || !rule->match_count || !rule->build_count ||
        rule->match_count > BS_MAX_RULE_NODES || rule->build_count > BS_MAX_RULE_NODES ||
        rule->guard_count > BS_MAX_RULE_NODES || !rule->guard_count || !rule->guards)
        return bs_error(error, BS_INVALID, rule->span, "invalid rule descriptor");
    unsigned sorts = 0;
    for (size_t i = 0; i < kernel->concern_count; ++i) {
        const BSConcern *v = bs_concern_find(registry, kernel->concerns[i].name);
        if (!v) return bs_error(error, BS_INVALID, kernel->concerns[i].span, "unknown HIR view %s", kernel->concerns[i].name ? kernel->concerns[i].name : "<null>");
        sorts |= 1u << v->sort;
    }
    if (!(sorts & (1u << rule->result))) return bs_error(error, BS_INVALID, rule->span, "result sort requires a concerns declaration");
    /* Validate enum values before using them as bit shifts. */
    for (size_t i = 0; i < rule->match_count; ++i)
        if (rule->match[i].sort != BS_TERM && rule->match[i].sort != BS_BINARY)
            return bs_error(error, BS_INVALID, rule->match[i].span, "unknown pattern sort");
    const BSCapability **imports = calloc(kernel->require_count, sizeof(*imports));
    const BSCapability **predicates = calloc(rule->guard_count, sizeof(*predicates));
    unsigned char *seen = calloc(BS_MAX_RULE_NODES, 1);
    if ((kernel->require_count && !imports) || !predicates || !seen) {
        free(imports); free(predicates); free(seen);
        return bs_error(error, BS_NOMEM, rule->span, "cannot allocate rule validation");
    }
    BSStatus status = BS_OK;
    for (size_t i = 0; i < kernel->require_count; ++i) {
        imports[i] = bs_requires_find(registry, kernel->requires[i].name);
        if (!imports[i]) { status = bs_error(error, BS_INVALID, kernel->requires[i].span, "unknown capability %s", kernel->requires[i].name ? kernel->requires[i].name : "<null>"); goto done; }
        for (size_t j = 0; j < i; ++j) if (imports[i] != imports[j] && names_overlap(imports[i], imports[j])) {
            status = bs_error(error, BS_INVALID, kernel->requires[i].span, "ambiguous imported predicate %s", imports[i]->alias); goto done;
        }
    }
    Check check = {registry, rule, seen, 0, 0, 0, sorts, NULL, error};
    status = match_check(&check, rule->match_count - 1, 1);
    if (status != BS_OK) goto done;
    if (check.visited != rule->match_count) { status = bs_error(error, BS_INVALID, rule->span, "unreachable match instructions"); goto done; }
    memset(seen, 0, BS_MAX_RULE_NODES); check.visited = 0;
    status = build_check(&check, rule->build_count - 1, 1);
    if (status != BS_OK) goto done;
    if (check.visited != rule->build_count) { status = bs_error(error, BS_INVALID, rule->span, "unreachable replacement instructions"); goto done; }
    if (rule->result == BS_BINARY && rule->build[rule->build_count - 1].kind != BS_BUILD_BINARY) {
        status = bs_error(error, BS_INVALID, rule->span, "Binary result requires a binary construction"); goto done;
    }
    int proof = 0;
    for (size_t i = 0; i < rule->guard_count; ++i) {
        const BSGuard *g = &rule->guards[i];
        if (!g->name) { status = bs_error(error, BS_INVALID, g->span, "missing predicate name"); goto done; }
        for (size_t j = 0; j < kernel->require_count; ++j)
            if (!strcmp(g->name, imports[j]->alias) || !strcmp(g->name, short_name(imports[j]->name))) predicates[i] = imports[j];
        if (!predicates[i]) { status = bs_error(error, BS_INVALID, g->span, "unknown or unimported predicate %s", g->name); goto done; }
        if (g->count != predicates[i]->arity || (g->count && !g->arguments)) {
            status = bs_error(error, BS_INVALID, g->span, "predicate %s expects %u arguments", g->name, predicates[i]->arity); goto done;
        }
        for (size_t j = 0; j < g->count; ++j) if (!g->arguments[j] || g->arguments[j] > check.captures) {
            status = bs_error(error, BS_INVALID, g->span, "guard capture out of range 1..%zu", check.captures); goto done;
        }
        if (check.domain && !strcmp(predicates[i]->proof_domain, check.domain)) proof = 1;
    }
    if (check.domain && strcmp(check.domain, "integer") && !proof) {
        status = bs_error(error, BS_INVALID, rule->span, "host rule requires an explicit transformation proof"); goto done;
    }
    status = copy_rule(rule, predicates, out, error);
    if (status == BS_OK) { out->captures = check.captures; out->binaries = check.binaries; }
done:
    free(imports); free(predicates); free(seen);
    return status;
}
