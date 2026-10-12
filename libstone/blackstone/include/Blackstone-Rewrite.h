#ifndef BLACKSTONE_REWRITE_H
#define BLACKSTONE_REWRITE_H
#include "Blackstone-Concern.h"
#include "Blackstone-Profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *rule;
    BSSpan rule_source;
    BSValue before, after;
    size_t step;
} BSTrace;
/* Callbacks inspect metadata only. Values and view strings remain alive for
 * the entire run. Construction is immutable; zero declines a candidate.
 * inspect/rebuild failures are adapter errors. equivalent detects no-change,
 * whereas identity is stable value identity (never structural equality).
 * fold implements exact-typed integer folding, declining exceptional division.
 * This driver normalizes occurrence trees/DAGs, not mutable SSA def-use graphs. */
typedef struct {
    uint32_t abi_version;
    void *context;
    int (*inspect)(void *, BSValue, BSValueView *);
    int (*identity)(void *, BSValue, BSValue);
    int (*equivalent)(void *, BSValue, BSValue);
    BSFact (*query)(void *, const char *, BSValue, const BSValue *, size_t,
                    const BSValue *, size_t);
    BSValue (*literal)(void *, const char *, BSValue source, const char *rule);
    BSValue (*binary)(void *, const char *, BSValue, BSValue, BSValue source, const char *rule);
    BSValue (*rebuild)(void *, BSValue original, BSValue, BSValue);
    BSValue (*fold)(void *, BSValue, const char *rule);
    void (*trace)(void *, const BSTrace *);
} BSAdapter;
typedef struct { size_t budget, max_depth; } BSRewriteOptions;
typedef struct { size_t rewrites, visited; } BSRewriteStats;

BSStatus bs_optimize(const BSProfile *, const BSAdapter *, BSValue root,
                     const BSRewriteOptions *, BSValue *out, BSRewriteStats *, BSError *);

#ifdef __cplusplus
}
#endif
#endif
