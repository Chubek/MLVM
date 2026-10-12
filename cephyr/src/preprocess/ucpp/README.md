# ucpp embedding adapter

`prepare.py SOURCE BUILD_COPY` copies the supplied dependency and applies
checked replacements. CMake tracks both the adapter and dependency inputs for
reconfigure. The original checkout is never changed.

Changes cover one embedding fix: bounding `#include` nesting at depth 64 with
a structured diagnostic. Upstream ucpp has no include-depth limit, so an
unguarded self-inclusion recurses until memory or time is exhausted. The
internal preprocessor reports such cycles at the same depth; the patched
message names the cycle explicitly.

Error reporting needs no source patch: the library builds with
`NO_UCPP_ERROR_FUNCTIONS` and Cephyr supplies `ucpp_error`, `ucpp_warning`,
and `ucpp_ouch` implementations that record structured diagnostics instead of
writing to stderr. `die()` (out-of-memory or output failure) keeps upstream
behavior; ordinary preprocessing failures use return codes.

`ucpp.cpp` serializes all runs with a process-wide mutex because ucpp keeps
global macro, include-path, and file-stack state. Each run ends with
`free_lexer_state` and `wipeout` so no state leaks between compilations.
