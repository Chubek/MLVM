#include "internal.h"
#include <stdlib.h>
#include <string.h>

const BSCapability *bs_requires_find(const BSRegistry *r, const char *name) {
    if (r && name) for (size_t i = 0; i < r->capability_count; ++i)
        if (!strcmp(r->capabilities[i].name, name)) return &r->capabilities[i];
    return NULL;
}
void bs_requires_truncate(BSRegistry *r, size_t count) {
    while (r->capability_count > count) {
        BSCapability *c = &r->capabilities[--r->capability_count];
        free((char *)c->name); free((char *)c->alias); free((char *)c->proof_domain);
    }
}
