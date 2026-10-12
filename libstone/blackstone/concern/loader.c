#include "internal.h"
#include "lexer.h"
#include <stdlib.h>

BSStatus bs_concern_load(BSRegistry *r, const BSConcernModule *m, BSError *error) {
    if (!r || !m || !m->name || (m->view_count && !m->views) || (m->operator_count && !m->operators) ||
        m->view_count > BS_MAX_RULE_NODES || m->operator_count > BS_MAX_RULE_NODES)
        return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid concern module");
    size_t views = r->view_count, operators = r->operator_count;
    BSStatus status = BS_OK;
    for (size_t i = 0; i < m->view_count; ++i) {
        const BSConcern *v = &m->views[i];
        if (!bs_qualified_valid(v->name) || (v->sort != BS_TERM && v->sort != BS_BINARY) || bs_concern_find(r, v->name)) {
            status = bs_error(error, BS_INVALID, (BSSpan){0}, "invalid or duplicate concern in %s", m->name); break;
        }
        BSConcern *next = realloc(r->views, (r->view_count + 1) * sizeof(*next));
        char *name = bs_string(v->name);
        if (next) r->views = next;
        if (!next || !name) { free(name); status = BS_NOMEM; break; }
        r->views[r->view_count++] = (BSConcern){name, v->sort};
    }
    for (size_t i = 0; status == BS_OK && i < m->operator_count; ++i) {
        const BSOperator *op = &m->operators[i];
        if (!bs_identifier_valid(op->name) || !op->domain || !*op->domain || bs_operator_find(r, op->name)) {
            status = bs_error(error, BS_INVALID, (BSSpan){0}, "invalid or duplicate operator in %s", m->name); break;
        }
        BSOperator *next = realloc(r->operators, (r->operator_count + 1) * sizeof(*next));
        char *name = bs_string(op->name), *domain = bs_string(op->domain);
        if (next) r->operators = next;
        if (!next || !name || !domain) { free(name); free(domain); status = BS_NOMEM; break; }
        r->operators[r->operator_count++] = (BSOperator){name, domain, op->associative, op->commutative};
    }
    if (status != BS_OK) bs_concern_truncate(r, views, operators);
    if (status == BS_NOMEM) return bs_error(error, status, (BSSpan){0}, "cannot allocate concern module");
    return status;
}
