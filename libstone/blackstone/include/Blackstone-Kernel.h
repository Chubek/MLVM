#ifndef BLACKSTONE_KERNEL_H
#define BLACKSTONE_KERNEL_H
#include "Blackstone-API.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { const char *name; BSSpan span; } BSDeclaration;
typedef enum {
    BS_RESOLVES = 1, BS_LITERAL = 2, BS_NONLITERAL = 3, BS_EXACT = 4, BS_SAME = 5
} BSRefinement;
/* A postorder signature/match tree. Binary nodes carry the backtick opcode;
 * Term nodes carry the assigned one-based capture and refinement. Child indices
 * are zero-based and precede their parent. The final node is the root. */
typedef struct {
    BSSort sort;
    BSRefinement refinement;
    uint32_t capture, same, left, right;
    const char *text;
    BSSpan span;
} BSMatchNode;
typedef enum { BS_CAPTURE = 1, BS_INTEGER = 2, BS_BUILD_BINARY = 3 } BSBuildKind;
typedef struct {
    BSBuildKind kind;
    uint32_t capture, left, right;
    const char *text;
    BSSpan span;
} BSBuildNode;
typedef struct {
    const char *name;
    const uint32_t *arguments;
    size_t count;
    BSSpan span;
} BSGuard;
typedef struct {
    const char *name;
    BSSort result;
    const BSMatchNode *match;
    size_t match_count;
    const BSBuildNode *build;
    size_t build_count;
    const BSGuard *guards;
    size_t guard_count;
    BSSpan span;
} BSRule;
typedef struct {
    const char *name;
    const BSDeclaration *inherits;
    size_t inherit_count;
    const BSDeclaration *concerns;
    size_t concern_count;
    const BSDeclaration *requires;
    size_t require_count;
    const BSRule *rules;
    size_t rule_count;
    BSSpan span;
} BSKernel;

#ifdef __cplusplus
}
#endif
#endif
