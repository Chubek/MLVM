"""Compile, verify MLVM IR, assemble, link and execute the integer C11 slice."""
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1]).resolve()


def run(tool, *args, status=0):
    result = subprocess.run([str(bin_dir / tool), *map(str, args)], capture_output=True, timeout=15)
    assert result.returncode == status, (tool, args, result.returncode, result.stdout, result.stderr)
    return result


programs = {
    "parameters": "int add(int a, int b) { return a+b; } int answer(void) { return add(19,23); }",
    "recursion": "int fact(int n) { if(n<=1) return 1; return n*fact(n-1); } int answer(void) { return fact(4)+18; }",
    "locals": "int answer(void) { int x=20; x+=1; int y=2; { int x=99; x--; } return x*y; }",
    "while": "int answer(void) { int i=0; int n=0; while(i<10) { ++i; if(i==3) continue; if(i==9) break; n+=i; } return n+9; }",
    "for": "int answer(void) { int n=0; for(int i=0;i<7;i++) { n+=i; } return n*2; }",
    "do": "int answer(void) { int n=0; do { n++; if(n<6) continue; } while(n<6); return n*7; }",
    "short_circuit": "int answer(void) { int n=0; 0 && ++n; 1 || ++n; return (n==0 && (1 || (1/0))) ? 42 : 9; }",
    "conditional": "int answer(void) { int n=0; int x=1 ? 42 : ++n; return n ? 3 : x; }",
    "signed": "int f(int a,int b) { return a/b+a%b; } int answer(void) { return f(-20,3)+50; }",
    "bits": "int f(int a,int b) { return ((a<<b)|3)^1; } int answer(void) { return f(5,3); }",
    "six_args": "int f(int a,int b,int c,int d,int e,int f) { return a+b+c+d+e+f; } int answer(void) { return f(1,2,3,4,5,27); }",
    "prototype": "static int f(int x); int answer(void) { return f(41); } static int f(int x) { return x+1; }",
    "void": "void f(void) { return; } int answer(void) { f(); return 42; }",
    "comma": "int answer(void) { int n=0; return (n=40, n+2); }",
    "literals": "int answer(void) { return 010 + 0xA + 0Xb + '\\n' + 3; }",
    "macros": "#define ADD(a, b) ((a) + (b))\n#define TWICE(x) ((x) * 2)\nint answer(void) { return TWICE(ADD(10, 11)); }",
    "c11": '_Static_assert(6*7==42,"arithmetic"); _Static_assert(1||(1/0),"short circuit"); _Static_assert(0?0:1,"conditional"); int answer(void) { int x=0; _Static_assert(1,"block"); return _Generic(x++,int:42,default:1/0)+x; }',
}

invalid = [
    "int f(void) { return @; }",
    "int f(void) { return 1 + ; }",
    "int f(void) { return 99999999999999999999999999999999999999999; }",
    "int f(void) { return 08; }",
    'int f(void) { "unterminated; }',
    "int f(int a) { int a=1; return a; }",
    "int f(void) { const int a=1; a=2; return a; }",
    "int f(void) { break; }",
    "int f(void) { continue; }",
    "int f(int a) { return a; } int answer(void) { return f(); }",
    "int f(void) { return missing(1); }",
    "int f(int); int f(int,int);",
    "int f(int a, ...);",
    'int f(void) { _Static_assert(0,"failed"); return 0; }',
    'int f(int x) { _Static_assert(x,"not constant"); return 0; }',
    '_Static_assert((1,2),"comma is not an ICE");',
    'int f(void) { return _Generic(1,int:2,int:3); }',
    'int f(void) { return _Generic(1,default:2,default:3); }',
    'int f(void) { return _Generic(1,int:2,default:undeclared); }',
    'int f(void); int answer(void) { int f=42; return f(); }',
    'static extern int f(void) { return 1; }',
    'int f(void) __attribute__((naked)) { return 1; }',
]

with tempfile.TemporaryDirectory(prefix="cephyr-test-") as directory:
    root = Path(directory)
    start = root / "start.s"
    start.write_text(".globl _start\n_start:\ncall answer\nmov %eax, %edi\nmov $60, %eax\nsyscall\n")
    run("mlvm-as", start, "-o", root / "start.o")
    native = platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")
    for name, source in programs.items():
        c = root / f"{name}.c"
        c.write_text(source)
        run("cephyr", "-std=c11", "--emit-ir", c, "-o", root / "test.mlvm")
        run("mlvm", "--verify", root / "test.mlvm")
        run("cephyr", "-S", c, "-o", root / "test.s")
        run("mlvm-as", root / "test.s", "-o", root / "asm.o")
        run("cephyr", "-c", c, "-o", root / "test.o")
        assert (root / "test.o").read_bytes() == (root / "asm.o").read_bytes()
        run("mlvm-ld", root / "start.o", root / "test.o", "-o", root / "test")
        if native:
            result = subprocess.run([str(root / "test")], timeout=5)
            assert result.returncode == 42, (name, result.returncode)
    for source in invalid:
        c = root / "invalid.c"
        c.write_text(source)
        result = run("cephyr", "-fsyntax-only", c, status=1)
        assert not result.stdout and b"error" in result.stderr and b"Sanitizer" not in result.stderr, result
    c.write_text("__STDC_VERSION__\n")
    assert b"201112L" in run("cephyr", "-E", c).stdout
    assert b"201112L" in run("cephyr", "-E", "--preprocessor=internal", c).stdout
    # The internal engine keeps the object-macro subset: function-like macros
    # are rejected there but accepted by the ucpp and system engines.
    c.write_text("#define ADD(a, b) ((a) + (b))\nint answer(void) { return ADD(19, 23); }\n")
    run("cephyr", "-fsyntax-only", "--preprocessor=internal", c, status=1)
    run("cephyr", "-fsyntax-only", c)
    run("cephyr", "--preprocessor=bogus", c, status=2)
    run("cephyr", "-fsyntax-only", "--preprocessor=system", "--system-preprocessor=/nonexistent-pp", c, status=1)
    if shutil.which("cc"):
        run("cephyr", "-fsyntax-only", "--preprocessor=system", c)
        c.write_text("__STDC_VERSION__\n")
        assert b"201112L" in run("cephyr", "-E", "--preprocessor=system", c).stdout
    # Independent System V ABI oracle: a host-compiled caller and callee around
    # a Cephyr function, with values that must survive a call in the middle.
    if native and shutil.which("cc"):
        c.write_text("int host_add(int a,int b); int cephyr_mix(int a,int b,int c,int d,int e,int f) { int x=a+b+c; int y=host_add(d,e); return x+y+f; }")
        run("cephyr", "-c", c, "-o", root / "abi.o")
        harness = root / "abi.c"
        harness.write_text("int cephyr_mix(int,int,int,int,int,int); int host_add(int a,int b) { return a+b; } int main(void) { return cephyr_mix(1,2,3,4,5,27)!=42; }")
        subprocess.run(["cc", str(harness), str(root / "abi.o"), "-o", str(root / "abi")], capture_output=True, check=True, timeout=15)
        subprocess.run([str(root / "abi")], check=True, timeout=5)
