#ifndef BLACKSTONE_PROFILE_IR_H
#define BLACKSTONE_PROFILE_IR_H
#include "parser.h"
#include "Blackstone-Requires.h"
typedef struct {
    BSRule rule;
    size_t captures, binaries;
    int folding;
    const BSCapability **predicates;
} BSCompiledRule;
#endif
