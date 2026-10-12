#include "internal.h"
#include "lexer.h"
#include <stdlib.h>

BSStatus bs_requires_load(BSRegistry *r, const BSRequiresModule *m, BSError *error) {
    if (!r || !m || !m->name || (m->count && !m->capabilities) || m->count > BS_MAX_RULE_NODES)
        return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid requires module");
    size_t count = r->capability_count;
    BSStatus status = BS_OK;
    for (size_t i = 0; i < m->count; ++i) {
        const BSCapability *c = &m->capabilities[i];
        if (!bs_qualified_valid(c->name) || !bs_identifier_valid(c->alias) ||
            c->arity > BS_MAX_RULE_NODES || bs_requires_find(r, c->name)) {
            status = bs_error(error, BS_INVALID, (BSSpan){0}, "invalid or duplicate capability in %s", m->name); break;
        }
        BSCapability *next = realloc(r->capabilities, (r->capability_count + 1) * sizeof(*next));
        char *name = bs_string(c->name), *alias = bs_string(c->alias), *domain = bs_string(c->proof_domain);
        if (next) r->capabilities = next;
        if (!next || !name || !alias || !domain) {
            free(name); free(alias); free(domain); status = BS_NOMEM; break;
        }
        r->capabilities[r->capability_count++] = (BSCapability){name, alias, c->arity, domain};
    }
    if (status != BS_OK) bs_requires_truncate(r, count);
    if (status == BS_NOMEM) return bs_error(error, status, (BSSpan){0}, "cannot allocate requires module");
    return status;
}
