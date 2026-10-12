#include "Blackstone-Concern.h"

const BSConcernModule *bs_loop_descriptor_module(void) {
    /* A descriptor is an opaque typed Term, not an executable loop body.
     * Transformations additionally need imported host proofs. */
    static const BSConcern views[] = {
        {"HIR::Loops::Descriptor", BS_TERM}, {"HIR::Loops::Operation", BS_BINARY}
    };
    static const BSConcernModule module = {"HIR::Loops", views, 2, NULL, 0};
    return &module;
}
