# 36. Cephyr preprocessing and parsing

[Contents](README.md) · [Next: Cephyr code generation](37-cephyr-codegen.md)

Cephyr is an optional C frontend using `third_party/cparser-iso11`.
Configure with `MLVM_ENABLE_CEPHYR=ON` to require its dependencies. The selected
language mode remains C11 freestanding; `cephyr/libc` supplies default headers
and a static runtime. `--sysroot` selects another runtime, `-nostdinc` disables
standard include paths, and user `-I` paths take precedence. The API exposes
the same selection through `PreprocessOptions::sysroot`/`standard_includes`.

```sh
build/cephyr -E examples/answer.c
build/cephyr -E --preprocessor=internal examples/answer.c
build/cephyr -E --preprocessor=system examples/answer.c
build/cephyr --parse-only examples/answer.c
build/cephyr -fsyntax-only examples/answer.c
```

These commands have different purposes: `-E` preprocesses, `--parse-only` checks
the parser grammar, and `-fsyntax-only` additionally validates the implemented
semantic subset. Parser acceptance is not a conformance or code-generation claim.

## Preprocessor engines

The default `--preprocessor=ucpp` engine links the vendored
`third_party/ucpp` C99 preprocessor with freestanding C11 predefined macros
(`__STDC__=1`, `__STDC_VERSION__=201112L`, `__STDC_HOSTED__=0`). It supports
function-like and variadic macros, `#`/`##`, general `#if` expressions,
`#elif`, `#include`/`#include_next`, `#line`, `#error`, and `#pragma`
passthrough. Include nesting is bounded at depth 64 with cycle diagnostics,
and output at 64 MiB.

`--preprocessor=system` instead invokes one external GCC-compatible
preprocessor (`-E -P -undef -std=c11 -ffreestanding -nostdinc` plus the user's
`-I`/`-D`/`-U`/`-include`). The command defaults to auto-detected `cc`, `gcc`,
`clang`, then `cpp`; `--system-preprocessor=COMMAND` names a single executable
instead. No shell is used. Host limits apply, and host warnings surface as
warnings.

`--preprocessor=internal` keeps the minimal deterministic object-macro engine:
object-like macros, recursive-expansion suppression, escaped-newline splicing,
comments and quoted tokens, `#undef`, quoted/angle includes, `#ifdef`/`#ifndef`,
`#if 0/1`, `#else`/`#endif`, and `#error`. CLI options include `-D`, `-U`, `-I`,
and `-include`. Function-like macros, stringification/token pasting, general
conditional expressions, `#elif`, `#line`, and pragmas are rejected.

## Source locations and limits

Line breaks are retained within one source, but include expansion and splicing
shift coordinates. Parser/lowering diagnostics refer to a separate
`<preprocessed>` buffer; exact original macro/include provenance is pending.

Expansion is bounded at depth 128 and 16 MiB per line. Includes are bounded at
depth 64 and total output at 64 MiB. Include cycles are diagnosed, including
recursion that would otherwise rely on guard handling. Avoid assuming a system
header set will work simply because the parser accepts much of its syntax.

The frontend is separately linkable as `MLVM::Cephyr`; clients do not need to
invoke a subprocess compiler. A tracked embedding adapter prepares a build-local
copy of cparser-iso11; no grammar regeneration or dependency-source edits occur.
`MLVM_CPARSER_SOURCE_DIR` selects an alternate checkout. See the feature matrix
for parser limitations and the implemented `_Static_assert`/`_Generic` subset.

Reference: [Cephyr feature matrix](../docs/CEPHYR.md), [frontend header](../cephyr/include/cephyr/frontend.hpp).
