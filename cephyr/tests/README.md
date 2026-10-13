# Cephyr regression tests

- `frontend.cpp`: embedding, preprocessing engines (internal/ucpp/system),
  diagnostics, parser failure recovery,
  MLVM IR round trips and invalid call/frame contracts.
- `execute.py`: C11 integer source → IR → assembly → ELF → native execution;
  includes structured control flow, recursion, six-argument calls, static
  assertions/generic selection, function-like macros, per-engine CLI checks,
  and optional host-compiler ABI interoperability.
- `driver.py`: emission aliases, dialect-target emission (`--target` with
  convertible modules and explicit blocking-operation failures), relocatable
  ELF, assembler/linker environment selection and CLI precedence,
  external-tool failures, overwrite protection,
  temporary cleanup, multi-source linking, and optional GNU `as`/`ld` interoperability.
- `kernels.py`: existing and user-authored Optilisp kernels through Cephyr's CLI,
  `-f` selection, JSON plans and relative paths, malformed plans and overwrite protection.
- `sched_actions.cpp`: `cephyr` scheduler action options, C pipelines through
  `target`, and explicit conversion/emission failures.
- `sched.py`: C pipelines through `mlvm-sched`/`mlvm --schedule` (`--check`,
  IR text, ELF objects, preprocessor options, failure and overwrite behavior).
- `../libc/tests/`: host-runtime self-test, Cephyr-built libc bootstrap,
  executable linking/sysroot selection and scalar/pointer ABI regressions.

Run `ctest --test-dir build -R cephyr --output-on-failure`. These are regression
tests for the documented implementation subset, not a full ISO C11 conformance suite.
