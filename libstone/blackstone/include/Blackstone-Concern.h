#ifndef BLACKSTONE_CONCERN_H
#define BLACKSTONE_CONCERN_H
#include "Blackstone-API.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { const char *name; BSSort sort; } BSConcern;
typedef struct { const char *name, *domain; int associative, commutative; } BSOperator;
typedef struct {
    const char *name;
    const BSConcern *views;
    size_t view_count;
    const BSOperator *operators;
    size_t operator_count;
} BSConcernModule;

/* Registration copies descriptors and strings. A module is installed atomically. */
BSStatus bs_concern_load(BSRegistry *, const BSConcernModule *, BSError *);
const BSConcern *bs_concern_find(const BSRegistry *, const char *qualified_name);
const BSOperator *bs_operator_find(const BSRegistry *, const char *name);
const BSConcernModule *bs_loop_descriptor_module(void);

typedef enum { BS_INPUT_VALUE = 1, BS_LITERAL_VALUE = 2, BS_BINARY_VALUE = 3 } BSValueKind;
typedef struct {
    BSValueKind kind;
    int resolved;
    const char *domain;
    uint32_t width;
    int is_signed;
    /* Optional canonical host type token, for additional type distinctions. */
    uintptr_t type_key;
    const char *op;
    const char *integer;
    size_t flag_count;
    BSValue left, right;
} BSValueView;

int bs_type_equal(const BSValueView *, const BSValueView *);

#ifdef __cplusplus
}
#endif
#endif
