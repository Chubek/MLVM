#!/usr/bin/env python3
"""Executable/runtime selection, LP64 semantics, and libc ABI regressions."""
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path


def run(*args, status=0, **kwargs):
    result = subprocess.run([str(arg) for arg in args], capture_output=True, timeout=30, **kwargs)
    if result.returncode != status:
        raise AssertionError(f"{args}: status {result.returncode}, expected {status}\n"
                             + result.stderr.decode(errors="replace"))
    return result


def main():
    compiler = Path(sys.argv[1]).resolve()
    native = platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")
    with tempfile.TemporaryDirectory(prefix="cephyr-runtime-") as directory:
        root = Path(directory)
        source = root / "main.c"
        source.write_text(r'''
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
extern int shared;
int narrow(char a, unsigned char b, short c, unsigned short d, int e, long f);
int later(void);
int main(int argc, char **argv) {
    unsigned long u = ULONG_MAX;
    long s = -9223372036854775807L;
    long high = 4294967296L;
    unsigned int ui = UINT_MAX;
    unsigned char byte = 255;
    bool truth = high;
    char text[13] = "hello";
    int array[3] = {7};
    if (argc != 2 || strcmp(argv[1], "argument") != 0) return 1;
    if (strcmp(getenv("CEPHYR_RUNTIME_TEST"), "present") != 0) return 2;
    if (u / 10UL != 1844674407370955161UL || u % 10UL != 5UL) return 3;
    if (u < 1UL || (u >> 63) != 1UL || ui < 1U || (ui >> 31) != 1U) return 4;
    if (s / 10L != -922337203685477580L || s % 10L != -7L) return 5;
    if (sizeof(1L) != 8 || sizeof(2147483648) != 8 || sizeof(0xffffffff) != 4) return 6;
    if (ULONG_MAX + 1UL != 0UL || UINT_MAX + 1U != 0U || !(ULONG_MAX > LONG_MAX)) return 7;
    if (sizeof(byte + byte) != 4 || byte + byte != 510 || !truth) return 8;
    if (!(high && high) || (high ? 0 : 1) || !high) return 9;
    if (text[12] != 0 || array[1] != 0 || array[2] != 0) return 10;
    if (narrow(-1, 255, -2, 65535, 9, 4294967296L) != 1) return 11;
    if (shared != 41 || later() != 42 || later() != 43) return 12;
    char *copy = strdup(text);
    if (copy == NULL || strcmp(copy, text) != 0) return 13;
    copy = realloc(copy, 64);
    if (copy == NULL || strcmp(copy, text) != 0) return 14;
    free(copy);
    char *zero = calloc(13, 1);
    if (zero == NULL || zero[0] != 0 || zero[12] != 0) return 15;
    free(zero);
    if (puts("cephyr runtime ok") < 0) return 16;
    char binary[7] = "a\0b\x21\101";
    if (binary[1] != 0 || binary[2] != 'b' || binary[3] != '!' || binary[4] != 'A' || binary[6] != 0) return 17;
    if (sizeof("a\0b") != 4 || strcmp("a" /* concat */ "b", "ab") != 0) return 18;
    return 0;
}
int later(void) { extern int shared; static int n = 41; return ++n + shared - 41; }
''')
        unit = root / "unit.c"
        unit.write_text('''int shared; int shared = 41;
int narrow(char a, unsigned char b, short c, unsigned short d, int e, long f) {
 return a == -1 && b == 255 && c == -2 && d == 65535 && e == 9 && f == 4294967296L;
}
''')
        obj = root / "unit.o"
        run(compiler, "-c", unit, "-o", obj)
        exe = root / "program"
        run(compiler, source, obj, "-o", exe)
        if native:
            result = run(exe, "argument", env={**os.environ, "CEPHYR_RUNTIME_TEST": "present"})
            assert result.stdout == b"cephyr runtime ok\n"
        run(compiler, source, unit, "-o", exe)
        if native:
            run(exe, "argument", env={**os.environ, "CEPHYR_RUNTIME_TEST": "present"})

        # Failed assertions must terminate through the real runtime.
        source.write_text('#include <assert.h>\nint main(void) { assert(0); return 7; }\n')
        run(compiler, source, "-o", exe)
        if native:
            assert b"assertion failed: 0" in run(exe, status=134).stderr
        source.write_text('#include <assert.h>\n#define NDEBUG\n#include <assert.h>\nint main(void) { assert(0); return 0; }\n')
        run(compiler, source, "-o", exe)
        if native:
            run(exe)

        # User -I precedes the standard include directory; -nostdinc disables it.
        custom = root / "custom"
        (custom / "include").mkdir(parents=True)
        (custom / "lib").mkdir()
        (custom / "include" / "stdlib.h").write_text('int abs(int value);\n#define CUSTOM_LIBC 1\n')
        source.write_text('#include <stdlib.h>\n#ifndef CUSTOM_LIBC\n#error wrong header\n#endif\nint main(void) { return 0; }\n')
        run(compiler, "-fsyntax-only", "-I", custom / "include", source)
        run(compiler, "-nostdinc", "-fsyntax-only", "-I", custom / "include", source)
        result = subprocess.run([str(compiler), "-nostdinc", "-fsyntax-only", str(source)], capture_output=True, timeout=30)
        assert result.returncode != 0

        # An explicit sysroot controls both headers and the archive/start files.
        start = root / "start.s"
        start.write_text('.globl _start\n_start:\ncall main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n')
        run(compiler.parent / "mlvm-as", start, "-o", custom / "lib" / "crt0.o")
        unit.write_text('int abs(int value) { return 17; }\n')
        run(compiler, "-c", unit, "-o", obj)
        run(compiler.parent / "mlvm-ar", "q", "-o", custom / "lib" / "libc.a", obj)
        source.write_text('#include <stdlib.h>\nint main(void) { return abs(-1) == 17 ? 0 : 1; }\n')
        run(compiler, "--sysroot", custom, source, "-o", exe)
        if native:
            run(exe)
        run(compiler, "-nostdlib", "-I", custom / "include", custom / "lib" / "crt0.o",
            source, custom / "lib" / "libc.a", "-o", exe)
        if native:
            run(exe)
        for literal in ("18446744073709551616ULL", "9223372036854775808", "1UU", "1lL", "09"):
            source.write_text(f"int main(void) {{ return {literal}; }}")
            result = subprocess.run([str(compiler), "-c", str(source), "-o", str(obj)], capture_output=True, timeout=30)
            assert result.returncode != 0, literal
    print("cephyr-libc: runtime, ABI, arithmetic, headers, and sysroot selection passed")


if __name__ == "__main__":
    main()
