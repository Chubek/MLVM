#ifndef BLACKSTONE_PROFILE_COMPILER_H
#define BLACKSTONE_PROFILE_COMPILER_H
#include "bytecode.h"
BSStatus bs_rule_compile(const BSRegistry *, const BSKernel *, const BSRule *,
                          BSCompiledRule *, BSError *);
void bs_rule_dispose(BSCompiledRule *);
BSStatus bs_declarations_check(const BSRegistry *, const BSKernel *, BSError *);
#endif
