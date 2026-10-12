#include "internal.h"
#include "compiler.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>

void bs_profile_destroy(BSProfile *profile) {
    if (!profile) return;
    for (size_t i = 0; i < profile->count; ++i) bs_rule_dispose(&profile->rules[i]);
    free(profile->rules); free(profile);
}
size_t bs_profile_rule_count(const BSProfile *p) { return p ? p->count : 0; }
const char *bs_profile_rule_name(const BSProfile *p, size_t i) { return p && i < p->count ? p->rules[i].rule.name : NULL; }

typedef struct {
    const BSKernel *kernels;
    size_t count;
    unsigned char *states;
    int folded;
    BSProfile *profile;
    BSError *error;
} Compile;

static BSStatus append(Compile *c, const BSKernel *kernel, const BSRule *rule) {
    BSProfile *p = c->profile;
    for (size_t i = 0; i < p->count; ++i) if (rule->name && !strcmp(p->rules[i].rule.name, rule->name))
        return bs_error(c->error, BS_INVALID, rule->span, "duplicate rule name %s in inheritance graph", rule->name);
    if (p->count >= BS_MAX_RULE_NODES) return bs_error(c->error, BS_INVALID, rule->span, "too many resolved rules");
    BSCompiledRule compiled = {0};
    BSStatus status;
    if (kernel) status = bs_rule_compile(p->registry, kernel, rule, &compiled, c->error);
    else {
        compiled.folding = 1; compiled.rule.name = bs_string("ConstantFolding");
        compiled.rule.span.path = bs_string("<builtin>");
        status = compiled.rule.name && compiled.rule.span.path ? BS_OK :
            bs_error(c->error, BS_NOMEM, rule->span, "cannot allocate ConstantFolding entry");
    }
    if (status != BS_OK) { bs_rule_dispose(&compiled); return status; }
    BSCompiledRule *next = realloc(p->rules, (p->count + 1) * sizeof(*next));
    if (!next) { bs_rule_dispose(&compiled); return bs_error(c->error, BS_NOMEM, rule->span, "cannot allocate profile"); }
    p->rules = next; p->rules[p->count++] = compiled;
    return BS_OK;
}

static BSStatus visit(Compile *c, const char *name, BSSpan span, size_t depth) {
    if (!name) return bs_error(c->error, BS_INVALID, span, "missing kernel name");
    if (depth > BS_MAX_DEPTH) return bs_error(c->error, BS_INVALID, span, "inheritance depth limit exceeded at %s", name);
    size_t index;
    for (index = 0; index < c->count; ++index) if (!strcmp(c->kernels[index].name, name)) break;
    if (index == c->count) {
        if (strcmp(name, "ConstantFolding")) return bs_error(c->error, BS_INVALID, span, "unknown inherited kernel %s", name);
        if (c->folded) return BS_OK;
        c->folded = 1;
        BSRule rule = {0}; rule.name = "ConstantFolding"; rule.span = span;
        return append(c, NULL, &rule);
    }
    if (c->states[index] == 1) return bs_error(c->error, BS_INVALID, span, "inheritance cycle at %s", name);
    if (c->states[index] == 2) return BS_OK;
    c->states[index] = 1;
    const BSKernel *k = &c->kernels[index];
    for (size_t i = 0; i < k->inherit_count; ++i) {
        BSStatus status = visit(c, k->inherits[i].name, k->inherits[i].span, depth + 1);
        if (status != BS_OK) return status;
    }
    BSStatus declarations = bs_declarations_check(c->profile->registry, k, c->error);
    if (declarations != BS_OK) return declarations;
    for (size_t i = 0; i < k->rule_count; ++i) {
        BSStatus status = append(c, k, &k->rules[i]);
        if (status != BS_OK) return status;
    }
    c->states[index] = 2;
    return BS_OK;
}

BSStatus bs_profile_compile(const BSRegistry *registry, const BSKernel *kernels, size_t count,
                            const char *root, BSProfile **out, BSError *error) {
    if (out) *out = NULL;
    if (error) *error = (BSError){0};
    if (!registry || !out || !root || !count || !kernels || count > BS_MAX_RULE_NODES)
        return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid kernel compilation input");
    for (size_t i = 0; i < count; ++i) {
        const BSKernel *k = &kernels[i];
        if (!bs_identifier_valid(k->name) || (k->inherit_count && !k->inherits) ||
            (k->concern_count && !k->concerns) || (k->require_count && !k->requires) || (k->rule_count && !k->rules) ||
            k->inherit_count > BS_MAX_RULE_NODES || k->concern_count > BS_MAX_RULE_NODES ||
            k->require_count > BS_MAX_RULE_NODES || k->rule_count > BS_MAX_RULE_NODES)
            return bs_error(error, BS_INVALID, k->span, "invalid kernel descriptor");
        for (size_t j = 0; j < i; ++j) if (!strcmp(k->name, kernels[j].name))
            return bs_error(error, BS_INVALID, k->span, "duplicate kernel %s", k->name);
    }
    BSProfile *profile = calloc(1, sizeof(*profile));
    unsigned char *states = calloc(count, 1);
    if (!profile || !states) { free(profile); free(states); return bs_error(error, BS_NOMEM, (BSSpan){0}, "cannot allocate profile"); }
    profile->registry = registry;
    Compile compile = {kernels, count, states, 0, profile, error};
    BSStatus status = visit(&compile, root, kernels[0].span, 1);
    free(states);
    if (status != BS_OK) bs_profile_destroy(profile);
    else { *out = profile; if (error) *error = (BSError){0}; }
    return status;
}
