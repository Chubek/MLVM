#ifndef BLACKSTONE_REQUIRES_H
#define BLACKSTONE_REQUIRES_H
#include "Blackstone-API.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name, *alias;
    uint32_t arity;
    /* Nonempty only for a proof of an entire structured-host transformation. */
    const char *proof_domain;
} BSCapability;
typedef struct {
    const char *name;
    const BSCapability *capabilities;
    size_t count;
} BSRequiresModule;

BSStatus bs_requires_load(BSRegistry *, const BSRequiresModule *, BSError *);
const BSCapability *bs_requires_find(const BSRegistry *, const char *qualified_name);

#ifdef __cplusplus
}
#endif
#endif
