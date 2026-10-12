#ifndef BLACKSTONE_PROFILE_H
#define BLACKSTONE_PROFILE_H
#include "Blackstone-Kernel.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves base-first, checks imports and instructions, and copies input IR.
 * The registry must outlive the immutable profile. ConstantFolding is built in
 * only when a kernel with that name is not present. */
BSStatus bs_profile_compile(const BSRegistry *, const BSKernel *, size_t count,
                            const char *root, BSProfile **out, BSError *);
void bs_profile_destroy(BSProfile *);
size_t bs_profile_rule_count(const BSProfile *);
const char *bs_profile_rule_name(const BSProfile *, size_t index);
/* Versioned, little-endian, pointer-free profile encoding. Reading revalidates
 * symbols and instruction trees against the supplied registry. */
BSStatus bs_profile_write(const BSProfile *, FILE *, BSError *);
BSStatus bs_profile_read(const BSRegistry *, FILE *, BSProfile **out, BSError *);

#ifdef __cplusplus
}
#endif
#endif
