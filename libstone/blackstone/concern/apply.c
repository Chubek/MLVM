#include "internal.h"
#include "lexer.h"
#include <string.h>

int bs_type_equal(const BSValueView *a, const BSValueView *b) {
    return a && b && a->domain && b->domain && !strcmp(a->domain, b->domain) &&
           a->width == b->width && a->is_signed == b->is_signed && a->type_key == b->type_key;
}

BSStatus bs_inspect(const BSAdapter *a, BSValue value, BSValueView *v, BSError *error) {
    *v = (BSValueView){0};
    if (!value || !a->inspect(a->context, value, v) || !v->domain || !*v->domain ||
        v->kind < BS_INPUT_VALUE || v->kind > BS_BINARY_VALUE ||
        (v->resolved && !v->width) || (v->is_signed != 0 && v->is_signed != 1) ||
        (v->kind == BS_BINARY_VALUE && (!v->left || !v->right || !v->op)) ||
        (v->kind == BS_LITERAL_VALUE && !bs_decimal_valid(v->integer)))
        return bs_error(error, BS_ADAPTER_ERROR, (BSSpan){0}, "adapter supplied an invalid value view");
    return BS_OK;
}

BSStatus bs_concern_apply(const BSRegistry *r, const BSAdapter *a, BSValue root,
                           const BSValue *nodes, size_t count, const char *domain,
                           int *valid, BSError *error) {
    BSValueView type;
    *valid = 0;
    BSStatus status = bs_inspect(a, root, &type, error);
    if (status != BS_OK) return status;
    if (!count || !type.resolved || !type.width || strcmp(type.domain, domain)) return BS_OK;
    for (size_t i = 0; i < count; ++i) {
        BSValueView v, left, right;
        if ((status = bs_inspect(a, nodes[i], &v, error)) != BS_OK) return status;
        const BSOperator *op = bs_operator_find(r, v.op);
        if (v.kind != BS_BINARY_VALUE || !v.resolved || !bs_type_equal(&type, &v) ||
            v.flag_count || !op || strcmp(op->domain, domain)) return BS_OK;
        if ((status = bs_inspect(a, v.left, &left, error)) != BS_OK ||
            (status = bs_inspect(a, v.right, &right, error)) != BS_OK) return status;
        if (!left.resolved || !right.resolved || !bs_type_equal(&type, &left) || !bs_type_equal(&type, &right)) return BS_OK;
    }
    *valid = 1;
    return BS_OK;
}
