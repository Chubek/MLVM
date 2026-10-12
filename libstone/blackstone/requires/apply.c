#include "internal.h"
#include <string.h>

BSStatus bs_requires_apply(const BSCapability *c, const BSRegistry *r, const BSAdapter *a,
                            BSValue root, const BSValue *args, size_t argc,
                            const BSValue *skeleton, size_t count, BSFact *out, BSError *error) {
    int modular = 0;
    *out = BS_UNKNOWN;
    /* The built-in domain contract cannot be overridden by a host's query. */
    if (!strcmp(c->name, "Arithmetic::Domains::ModularInteger")) {
        BSStatus status = bs_concern_apply(r, a, root, skeleton, count, "integer", &modular, error);
        if (status != BS_OK) return status;
        if (!modular) { *out = BS_FALSE; return BS_OK; }
    }
    if (a->query) {
        *out = a->query(a->context, c->alias, root, args, argc, skeleton, count);
        if (*out != BS_TRUE && *out != BS_FALSE && *out != BS_UNKNOWN)
            return bs_error(error, BS_ADAPTER_ERROR, (BSSpan){0}, "predicate %s returned an invalid fact", c->alias);
        return BS_OK;
    }
    if (modular) *out = BS_TRUE;
    else if (!strcmp(c->name, "Relational::Properties::Associativity") ||
             !strcmp(c->name, "Relational::Properties::Commutativity")) {
        BSStatus status = bs_concern_apply(r, a, root, &root, 1, "integer", &modular, error);
        if (status != BS_OK) return status;
        BSValueView view;
        if ((status = bs_inspect(a, root, &view, error)) != BS_OK) return status;
        const BSOperator *op = bs_operator_find(r, view.op);
        *out = modular && op && (!strcmp(c->alias, "Associative") ? op->associative : op->commutative) ? BS_TRUE : BS_FALSE;
    }
    return BS_OK;
}
