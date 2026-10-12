#!/usr/bin/env python3
"""Build cephyr-libc with Cephyr itself and run its self-test.

The host toolchain compiles the library too, but this harness proves the C
sources stay inside the subset Cephyr lowers: every function under test is
compiled by Cephyr, linked against the same start stub the execute test uses,
and run natively.
"""
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

START = """.globl _start
_start:
call cephyr_selftest
mov %eax, %edi
mov $60, %eax
syscall
"""


def run(command, **kwargs):
    result = subprocess.run([str(part) for part in command], capture_output=True, **kwargs)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
    return result


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: bootstrap.py CEPHYR LIBC_DIR\n")
        return 2
    cephyr = Path(sys.argv[1]).resolve()
    libc = Path(sys.argv[2]).resolve()
    sources = [str(libc / "src" / name) for name in (
        "string/mem.c", "string/str.c", "string/strtok.c", "string/strcase.c",
        "ctype/ctype.c", "errno/errno.c", "stdlib/abs.c", "stdlib/conv.c",
        "stdlib/rand.c", "stdlib/intmax.c", "assert/assert.c")]
    sources.append(str(libc / "tests" / "bootstrap.c"))
    native = platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")

    with tempfile.TemporaryDirectory(prefix="cephyr-libc-") as directory:
        root = Path(directory)
        start = root / "start.s"
        start.write_text(START)
        if run([cephyr.parent / "mlvm-as", start, "-o", root / "start.o"]).returncode != 0:
            return 1
        objects = []
        for index, source in enumerate(sources):
            obj = root / f"unit{index}.o"
            # --sysroot keeps the bundled headers authoritative for this run.
            result = run([cephyr, "--sysroot=" + str(libc), "-c", source, "-o", obj])
            if result.returncode != 0:
                sys.stderr.write(f"cephyr could not compile {source}\n")
                return 1
            objects.append(obj)
        if not native:
            print("cephyr-libc: compiled with Cephyr; skipping execution off x86-64 Linux")
            return 0
        if run([cephyr, "-nostartfiles", root / "start.o", *objects, "-o", root / "selftest"]).returncode != 0:
            return 1
        result = subprocess.run([str(root / "selftest")], timeout=30)
        if result.returncode != 0:
            sys.stderr.write(f"cephyr-libc self-test failed check {result.returncode}\n")
            return 1
    print(f"cephyr-libc: {len(sources)} translation units compiled by Cephyr; self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
