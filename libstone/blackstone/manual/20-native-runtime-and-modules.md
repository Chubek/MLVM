# Chapter 20 — The native runtime and modules

The C11 runtime is built as `libblackstone` and exposed as the CMake target
`Blackstone::Runtime`. Its six public headers define statuses, registry modules,
typed views, source IR, compiled profiles and rewrite callbacks. Include
`Blackstone-Rewrite.h` for the profile/adapter/optimization boundary and
`Blackstone-Requires.h` when registering capability modules.

## A working direct-C example

```sh
cmake --build build --target blackstone-native-example --parallel
./build/blackstone-native-example
```

[`native_profile.c`](../examples/native_profile.c) uses only public headers.
It compiles `x plus 0 -> x`, writes and reloads the profile, and supplies immutable
host node pointers as nonzero `BSValue` handles. Its result is the original input
pointer with unsigned-8-bit type and one trace event.

This forwarding-only rule needs inspection and tracing callbacks. It uses the
runtime's modular query fallback and does not request new literals, binaries or
folding. More capable integrations supply those callbacks explicitly.

## Registry lifecycle

1. Create the registry with `bs_registry_create`.
2. Load concern/operator descriptors with `bs_concern_load`.
3. Load capability descriptors with `bs_requires_load`.
4. Compile source IR or read a profile.
5. Optimize with a host adapter.
6. Destroy profiles before `bs_registry_destroy`.

A fresh C registry contains the seven integer operators, expression Term/Binary
views, five acceptance capabilities and loop descriptor views. The Python bridge
loads the extended frontend catalog into it. A direct C host registers its
additional views, opcodes and proof contracts itself.

Module loaders copy names and descriptors and roll back an unsuccessful module
load atomically. Lookup functions return borrowed registry entries whose addresses
may be invalidated by later registration. Do not mutate the registry concurrently
with compilation or optimization.

## Value inspection and callbacks

`BSValueView` contains kind, resolvedness, domain, width, signedness, an optional
canonical `type_key`, opcode, literal decimal spelling, flag count and operands.
All view strings and handles remain alive throughout a run. The host certifies
that a concrete literal's spelling is numerically representable in its exact type.

| Callback | Responsibility |
| --- | --- |
| `inspect` | Supply a valid resolved or unresolved value view |
| `identity` | Stable value identity; default is handle equality |
| `equivalent` | Optional no-change comparison, respecting opaque identity |
| `query` | Tri-state capability facts in the provided match context |
| `literal`, `binary` | Exact-typed immutable construction or zero to decline |
| `rebuild` | Preserve an existing operator's metadata with normalized operands |
| `fold` | Exact-type integer folding, declining exceptional division |
| `trace` | Observe a successful event with rule location and value handles |

Set `adapter.abi_version = BS_ABI_VERSION`. The runtime validates construction
types/sorts and eligible carrier metadata; host code implements arithmetic,
analysis facts, provenance and allocation. Without `query`, only built-in modular
eligibility and registered root associativity/commutativity can be established.

The host owns constructed values, including unused candidates. `bs_optimize`
publishes an output handle only on `BS_OK`; errors and nonconvergence leave it
zero. Independent runs can share an immutable profile/registry when their adapters
and contexts are thread-safe. See the [native API guide](../docs/native-api.md)
for the complete ABI and lifetime reference.

[Previous](19-python-frontend-and-adapters.md) · [Contents](README.md) · [Next: Native profiles](21-native-ir-profiles-and-persistence.md)
