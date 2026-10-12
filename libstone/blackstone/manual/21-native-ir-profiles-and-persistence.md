# Chapter 21 — Native IR, profiles and persistence

Native compilation consumes `BSKernel` descriptors. A frontend lowers the source
AST to these descriptors; the runtime independently resolves declarations,
inheritance and typed instruction contracts. The source grammar remains dparser's
grammar, while the internal profile headers define the lowering/compiler boundary.

## Postorder instruction trees

`BSMatchNode` combines signature structure and match opcodes. Its child indices
are zero-based, precede their parent, and refer to an unshared instruction tree.
The final node is the root. Term captures are one-based and assigned left-to-right.
Do not confuse these two indexing conventions.

For `Binary(Term!resolves, Term!exact(0))` matching `$1 plus $2`:

| Instruction index | Node | Information |
| ---: | --- | --- |
| 0 | Term | Capture 1, `BS_RESOLVES` |
| 1 | Term | Capture 2, `BS_EXACT`, decimal text `0` |
| 2 | Binary | Opcode `plus`, children 0 and 1 |

`BSBuildNode` uses `BS_CAPTURE`, `BS_INTEGER` or `BS_BUILD_BINARY`. Capture nodes
refer to assigned slots; binary nodes refer to earlier build instructions.
Reusing a capture creates another instruction referring to the same slot, not a
shared/cyclic instruction array. `BSGuard` stores a name, capture arguments and
location in source conjunction order.

The frontend must first establish signature/match correspondence while lowering
the two parsed trees. The native compiler checks instruction reachability,
numbering, refinements, references, registered names, arities and result shape.

## Compiled ownership and ordering

`bs_profile_compile` takes a registry, kernel array, selected root name, output
profile pointer and error object. It copies instruction/name/location data,
resolves aliases and preserves base/source rule order. Frontend storage can be
released immediately after successful compilation. The registry outlives the
profile; `bs_profile_destroy` releases profile-owned data.

`bs_profile_rule_count` and `bs_profile_rule_name` expose the combined order.
The native limits are 4,096 resolved rules, 4,096 instructions per tree and
instruction/inheritance depth 256. Rewrite options independently select budget
and an expression-depth bound no larger than `BS_MAX_DEPTH`.

## Save and reload

The runnable C example uses these public operations:

```c
/* Given valid compiled/registry/file values and a zero-initialized error. */
BSProfile *loaded = NULL;
if (bs_profile_write(compiled, file, &error) == BS_OK) {
    rewind(file);
    if (bs_profile_read(registry, file, &loaded, &error) == BS_OK) {
        /* loaded is ready for bs_optimize; destroy it when finished. */
        bs_profile_destroy(loaded);
    }
}
```

The v1 encoding starts with `BSTP` and version bytes `01 00 00 00`. Numbers are
unsigned little-endian 32-bit fields; strings are length-prefixed bytes without
NUL. The bounded encoding stores resolved rule order, folding markers, locations,
match/build trees, guards and qualified capability names. It stores no host
pointers or executable callback code.

Reading revalidates the instructions and required contracts against the supplied
registry. Truncated, malformed, oversized or unsupported encodings are errors;
the maximum file size is 16 MiB, and trailing bytes are rejected. A profile stream
contains one profile. A failed read publishes no partial profile.

Profile persistence caches validated rule instructions, not analysis facts or
host object identities. A compiler's cache key must also track the source and
the semantic contract versions supplied by its registry/adapter.

[Previous](20-native-runtime-and-modules.md) · [Contents](README.md) · [Next: Structured hosts](22-structured-host-kernels.md)
