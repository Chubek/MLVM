#ifndef BLACKSTONE_INTERNAL_H
#define BLACKSTONE_INTERNAL_H
#include "Blackstone-Concern.h"
#include "Blackstone-Requires.h"
#include "Blackstone-Rewrite.h"

struct BSRegistry {
    BSConcern *views;
    size_t view_count;
    BSOperator *operators;
    size_t operator_count;
    BSCapability *capabilities;
    size_t capability_count;
};
BSStatus bs_error(BSError *, BSStatus, BSSpan, const char *format, ...);
char *bs_string(const char *);
void bs_concern_truncate(BSRegistry *, size_t views, size_t operators);
void bs_requires_truncate(BSRegistry *, size_t count);
BSStatus bs_inspect(const BSAdapter *, BSValue, BSValueView *, BSError *);
BSStatus bs_concern_apply(const BSRegistry *, const BSAdapter *, BSValue,
                           const BSValue *, size_t, const char *domain, int *, BSError *);
BSStatus bs_requires_apply(const BSCapability *, const BSRegistry *, const BSAdapter *,
                            BSValue, const BSValue *, size_t, const BSValue *, size_t,
                            BSFact *, BSError *);
typedef struct { BSValue key, value; unsigned state; } BSMapEntry;
typedef struct { BSMapEntry *entries; size_t capacity, count; } BSMap;
BSMapEntry *bs_map_get(BSMap *, BSValue, int create);
void bs_map_dispose(BSMap *);
#endif
