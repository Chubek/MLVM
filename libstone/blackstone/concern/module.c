#include "internal.h"
#include <stdlib.h>
#include <string.h>

const BSConcern *bs_concern_find(const BSRegistry *r, const char *name) {
    if (r && name) for (size_t i = 0; i < r->view_count; ++i)
        if (!strcmp(r->views[i].name, name)) return &r->views[i];
    return NULL;
}

const BSOperator *bs_operator_find(const BSRegistry *r, const char *name) {
    if (r && name) for (size_t i = 0; i < r->operator_count; ++i)
        if (!strcmp(r->operators[i].name, name)) return &r->operators[i];
    return NULL;
}

void bs_concern_truncate(BSRegistry *r, size_t views, size_t operators) {
    while (r->view_count > views) free((char *)r->views[--r->view_count].name);
    while (r->operator_count > operators) {
        BSOperator *op = &r->operators[--r->operator_count];
        free((char *)op->name); free((char *)op->domain);
    }
}
