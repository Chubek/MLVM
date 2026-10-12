#ifndef BLACKSTONE_PROFILE_BYTECODE_H
#define BLACKSTONE_PROFILE_BYTECODE_H
#include "ir.h"
/* Typed, validated postorder instructions are the v0 bytecode. Opcodes are
 * BSSort/BSRefinement and BSBuildKind, rather than unchecked function pointers. */
struct BSProfile {
    const BSRegistry *registry;
    BSCompiledRule *rules;
    size_t count;
};
#endif
