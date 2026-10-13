#pragma once
// Blackstone's C ABI names this member `requires`, a C++20 keyword.
// Rename only while parsing the C declarations; layout and C linkage are unchanged.
#pragma push_macro("requires")
#undef requires
#define requires requires_declarations
#include <Blackstone-Rewrite.h>
#include <Blackstone-Requires.h>
#pragma pop_macro("requires")
