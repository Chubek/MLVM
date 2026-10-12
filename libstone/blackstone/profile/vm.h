#ifndef BLACKSTONE_PROFILE_VM_H
#define BLACKSTONE_PROFILE_VM_H
#include "bytecode.h"
#include "Blackstone-Rewrite.h"
typedef struct {
    BSValue *captures, *skeleton;
    size_t capture_count, skeleton_count;
} BSMatch;
BSStatus bs_match_rule(const BSCompiledRule *, const BSAdapter *, BSValue,
                       BSMatch *, int *matched, BSError *);
void bs_match_dispose(BSMatch *);
BSStatus bs_rule_candidate(const BSProfile *, const BSCompiledRule *,
                           const BSAdapter *, BSValue, BSValue *, BSError *);
#endif
