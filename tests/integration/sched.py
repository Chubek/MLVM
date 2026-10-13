"""Execute emitted artifacts with independent native, LLVM and WebAssembly consumers."""
import ctypes
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

tool = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()


def run(*args, ok=True, **kwargs):
    result = subprocess.run([str(a) for a in args], capture_output=True, **kwargs)
    assert (result.returncode == 0) == ok, (args, result.returncode, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="mlvm-sched-") as tmp:
    tmp = pathlib.Path(tmp)
    for example in (root / "examples/sched").glob("*.sched"):
        run(tool, example, "--check")
    algebra = root / "examples/sched/algebra.sched"
    result = run(tool, algebra, "--trace", cwd=tmp)
    assert result.stdout == b"42\n"
    assert b"ReassociateAddConstants" in result.stderr and b"ConstantFolding" in result.stderr
    run(tool.with_name("mlvm"), f"--schedule={algebra}", cwd=tmp)
    targets = root / "examples/sched/targets.sched"
    for name, suffix in (("native", "o"), ("wasm", "wasm"), ("llvm", "ll")):
        run(tool, targets, f"--result={name}", "-o", tmp / f"pipeline.{suffix}")

    cc = shutil.which("cc")
    if cc and platform.machine() in ("x86_64", "AMD64") and sys.platform == "linux":
        run(cc, "-shared", tmp / "pipeline.o", "-o", tmp / "pipeline.so")
        native = ctypes.CDLL(str(tmp / "pipeline.so")).pipeline
        native.argtypes, native.restype = [ctypes.c_int32], ctypes.c_int32
        for value in (-2147483648, -9, 0, 35, 2147483647):
            assert native(value) == ctypes.c_int32(value + 7).value
    clang = shutil.which("clang")
    if clang and cc and platform.machine() in ("x86_64", "AMD64") and sys.platform == "linux":
        run(clang, "-shared", "-fPIC", tmp / "pipeline.ll", "-o", tmp / "llvm.so")
        native = ctypes.CDLL(str(tmp / "llvm.so")).pipeline
        native.argtypes, native.restype = [ctypes.c_int32], ctypes.c_int32
        assert native(35) == 42
    node = shutil.which("node")
    if node:
        result = run(node, "-e", """
const fs = require('fs');
const bytes = fs.readFileSync(process.argv[1]);
if (!WebAssembly.validate(bytes)) throw Error('invalid wasm');
const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes));
for (const x of [-2147483648, -9, 0, 35, 2147483647]) {
  if (instance.exports.pipeline(x) !== ((x + 7) | 0)) throw Error('wrong result');
}
""", tmp / "pipeline.wasm")

    # A failed run must neither create nor truncate the selected output.
    bad = tmp / "bad.sched"
    bad.write_text("lsched.node{name='a', action='ir', options={path='missing.mlvm'}}\n")
    output = tmp / "out.txt"
    output.write_text("retained")
    run(tool, bad, "-o", output, ok=False)
    assert output.read_text() == "retained"
    bad.write_text("lsched.node{name='a', action='verify', inputs={'b'}}\n"
                   "lsched.node{name='b', action='verify', inputs={'a'}}\n")
    result = run(tool, bad, "--check", ok=False)
    assert b"cycle" in result.stderr
    run(tool, targets, "--result=missing", ok=False)

    # Protect IR dependencies as well as the orchestrator from output overwrite.
    shutil.copy(root / "examples/sched/input.mlvm", tmp / "input.mlvm")
    before = (tmp / "input.mlvm").read_bytes()
    bad.write_text("require('lsched').pipeline():ir('input', {path='input.mlvm'})\n")
    run(tool, bad, "-o", tmp / "input.mlvm", ok=False)
    assert (tmp / "input.mlvm").read_bytes() == before

    # Kaguya/Lua errors retain the script name and line.
    bad.write_text("local p = require('lsched').pipeline()\nerror('marker')\n")
    result = run(tool, bad, "--check", ok=False)
    assert b"bad.sched:2:" in result.stderr and b"marker" in result.stderr
