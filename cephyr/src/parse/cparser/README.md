# cparser-iso11 embedding adapter

`prepare.py SOURCE BUILD_COPY` copies the supplied dependency and applies checked
replacements. CMake tracks both the adapter and dependency inputs for reconfigure.
The original checkout is never changed. This is source adaptation, not grammar
generation; no external patch program is required.

Changes cover exception-based diagnostics, stream ownership, orphan AST cleanup,
thread-local null-node/lifetime bookkeeping, checked integer literals, terminated
strings/comments, unary signs, ordered block items, comma expressions, parameter
retention, declaration specifiers, C11 assertion/generic nodes, and rejection of
upstream constructs that would discard semantics.

AST ownership remains the upstream reference-counted graph. After the parser is
destroyed, Cephyr reclaims unattached zero-reference roots left by failed
productions. No dependency AST escapes `parse` or `lower`.

The supported frontend remains an integer C11 slice; see `docs/CEPHYR.md`.
Do not infer conformance from the dependency name or from parse-only acceptance.
