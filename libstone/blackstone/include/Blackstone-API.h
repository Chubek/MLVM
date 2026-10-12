#ifndef BLACKSTONE_API_H
#define BLACKSTONE_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BS_ABI_VERSION 1u
#define BS_MAX_DEPTH 256u
#define BS_MAX_RULE_NODES 4096u

typedef enum {
    BS_OK = 0, BS_INVALID = 1, BS_NOMEM = 2, BS_NONCONVERGENCE = 3,
    BS_ADAPTER_ERROR = 4, BS_IO_ERROR = 5
} BSStatus;
typedef enum { BS_UNKNOWN = -1, BS_FALSE = 0, BS_TRUE = 1 } BSFact;
typedef enum { BS_TERM = 1, BS_BINARY = 2 } BSSort;
typedef struct { const char *path; uint32_t line, column; } BSSpan;
typedef struct { BSStatus code; char message[1024]; } BSError;
typedef struct BSRegistry BSRegistry;
typedef struct BSProfile BSProfile;
/* Nonzero, stable host handles. The host owns all values, including new ones. */
typedef uintptr_t BSValue;

uint32_t bs_abi_version(void);
BSRegistry *bs_registry_create(BSError *error);
void bs_registry_destroy(BSRegistry *registry);

#ifdef __cplusplus
}
#endif
#endif
