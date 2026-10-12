"""Prepare a build-local ucpp copy with embedding fixes.

The downloaded tree is never modified. Replacements fail closed when the
upstream source layout changes.
"""
from pathlib import Path
import shutil
import sys

source, target = map(Path, sys.argv[1:])
target.mkdir(parents=True, exist_ok=True)
MARKER = "cephyr-adapter.json"
ADAPTED = "cephyr-ucpp"
names = ["mem.c", "nhash.c", "cpp.c", "lexer.c", "assert.c", "macro.c",
         "eval.c", "arith.c", "cpp.h", "ucppi.h", "mem.h", "nhash.h", "arith.h",
         "config.h", "tune.h"]
if (source / MARKER).is_file():
    for name in names + ["LICENSE", MARKER]:
        if (source / name).is_file():
            shutil.copyfile(source / name, target / name)
    print(f"ucpp already adapted: {source}")
    sys.exit(0)

sources = ["mem.c", "nhash.c", "cpp.c", "lexer.c", "assert.c", "macro.c",
           "eval.c", "arith.c"]
headers = ["cpp.h", "ucppi.h", "mem.h", "nhash.h", "arith.h", "config.h",
           "tune.h"]
for name in sources + headers:
    shutil.copyfile(source / name, target / name)
shutil.copyfile(source / "LICENSE", target / "LICENSE")


def replace(text, old, new):
    if old not in text:
        raise RuntimeError(f"ucpp adapter mismatch: {old[:100]!r}")
    return text.replace(old, new, 1)


path = target / "cpp.c"
text = path.read_text()
# Bound include nesting like the internal preprocessor (depth 64) and report
# include cycles instead of recursing until resources are exhausted. Upstream
# has no include-depth limit; unguarded self-inclusion otherwise never ends.
text = replace(
    text,
    """do_include_next:
	if (!(ls->flags & LEXER) && (ls->flags & KEEP_OUTPUT))
		put_char(ls, '\\n');
	push_file_context(ls);""",
    """do_include_next:
	if (!(ls->flags & LEXER) && (ls->flags & KEEP_OUTPUT))
		put_char(ls, '\\n');
	if (ls_depth >= 64) {
		error(l, "include nesting exceeds 64 (possible include cycle)");
		freemem(fname);
		return 1;
	}
	push_file_context(ls);""",
)
path.write_text(text)
(target / MARKER).write_text(ADAPTED + "\n")
