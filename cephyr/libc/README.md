# Cephyr libc

Default static runtime for Cephyr on **x86-64 Linux, LP64**. The host compiler
builds the archive and startup object; the bootstrap suite recompiles the
scalar/string core with Cephyr and executes those objects against the runtime.

## Build and use

```sh
cmake -S ../.. -B build -DMLVM_ENABLE_CEPHYR=ON
cmake --build build --parallel
ctest --test-dir build -R cephyr-libc --output-on-failure
build/cephyr main.c -o main
```

From the MLVM root, use `cmake -S .` instead. Artifacts:

- `cephyr-sysroot/include/`: standard headers;
- `cephyr-sysroot/lib/libc.a`: static archive, CMake target `MLVM::CephyrLibc`;
- `cephyr-sysroot/lib/crt0.o`: Linux entry, argc/argv/envp setup, call to `main`;
- installation: `<prefix>/share/mlvm/cephyr-libc/{include,lib}`.

`MLVM_CEPHYR_SYSROOT=/path` selects an external runtime at compiler-build time;
`--sysroot=/path` overrides it at invocation time. `MLVM_CEPHYR_LIBC=OFF` disables
the bundled runtime. The external layout and linker contract are documented in
[CEPHYR.md](../../docs/CEPHYR.md#runtime-and-sysroot-selection).

## Coverage

| Area | Implemented surface |
| --- | --- |
| Memory/strings | Copy, move, fill, compare, search, concatenate, spans, tokenization, duplication, bounded copies, C-locale `strcoll`/`strxfrm` |
| Integer conversion | `strtol`, `strtoul`, `strtoll`, `strtoull`, `strtoimax`, `strtoumax`; bases 2–36, signs, prefix recognition, end pointers, overflow saturation and `ERANGE` |
| Integer utilities | `abs`/`labs`/`llabs`/`imaxabs`, host aggregate division, `rand`/`srand`/`rand_r` |
| Character classification | C-locale `<ctype.h>` functions |
| Process/runtime | Startup, environment, exit handlers, termination, assertion termination, single-threaded errno |
| Allocation | Host-built first-fit arena, `malloc`/`calloc`/`realloc`/`free`; callable from Cephyr |
| I/O | Host-built unbuffered streams and Linux syscall wrappers; scalar/pointer APIs callable from Cephyr |
| Retained host facilities | Sorting/search callbacks, formatting, floating conversion, math, time, setjmp and architecture CRT sources |

The original runtime sources are retained. Retained facilities are **not a
complete conformance claim**: notably math contains unfinished routines,
floating conversion/formatting and time support are partial, and termination
currently uses Linux exit statuses rather than a full signal subsystem.
Threading, TLS errno, locale switching and a complete C11 hosted environment
remain future work. `__STDC_HOSTED__` remains zero.

Headers compiled under `__CEPHYR__` expose the supported ABI. `<stdio.h>` treats
`FILE` as opaque and hides variadic declarations; `<stdlib.h>` hides callback,
aggregate-return and floating APIs. Those declarations remain present for host
compilers. `<inttypes.h>` adds integer conversion and basic fixed-width/MAX/PTR
format macros; scanning and the full least/fast macro family remain pending.

## Verification

- `cephyr-libc-host`: restored freestanding self-test, linked without host libc.
- `cephyr-libc-bootstrap`: 12 Cephyr-built translation units; string, integer,
  tokenization, ctype, overflow/end-pointer and boundary checks.
- `cephyr-libc-runtime`: default/external sysroots, header precedence,
  multi-input linking, argc/argv/environment, allocation/output/assertions,
  global/static storage, narrow-argument ABI and 64-bit arithmetic.

Frontend limitations remain explicit in [CEPHYR.md](../../docs/CEPHYR.md).
