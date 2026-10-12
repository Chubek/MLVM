"""Driver stopping points, tool overrides, failures and external interoperability."""
from pathlib import Path
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1]).resolve()
environment = dict(os.environ)
environment.pop("CEPHYR_AS", None)
environment.pop("CEPHYR_LD", None)

with tempfile.TemporaryDirectory(prefix="cephyr-driver-") as directory:
    root = Path(directory)
    source = root / "main.c"
    source.write_text("int helper(int); int main(void) { return helper(41); }")
    helper = root / "helper.c"
    helper.write_text("int helper(int x) { return x+1; }")
    # A tiny sysroot also lets this suite run in builds without bundled libc.
    runtime = root / "runtime"
    (runtime / "lib").mkdir(parents=True)
    (runtime / "include").mkdir()
    startup = root / "start.s"
    startup.write_text(".globl _start\n_start:\ncall main\nmov %eax, %edi\nmov $60, %eax\nsyscall\n")
    subprocess.run([str(bin_dir / "mlvm-as"), str(startup), "-o", str(runtime / "lib/crt0.o")], check=True)
    (runtime / "lib/libc.a").write_bytes(b"!<arch>\n")

    def run(*args, status=0, env=None):
        result = subprocess.run([str(bin_dir / "cephyr"), f"--sysroot={runtime}", *map(str, args)],
                                cwd=root, env=environment | (env or {}),
                                capture_output=True, timeout=20)
        assert result.returncode == status, (args, result.returncode, result.stderr)
        return result.stdout

    def execute(path):
        if platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64"):
            assert subprocess.run([str(path)], timeout=5).returncode == 42

    # Stopping before assembly/linking must not consult unavailable tools.
    missing = {"CEPHYR_AS": "/nonexistent-cephyr-as", "CEPHYR_LD": "/nonexistent-cephyr-ld"}
    run("-i", source, "-o", root / "main.mlvm", env=missing)
    run("-s", source, "-o", root / "main.s", env=missing)
    assert b"mlvm 1" in (root / "main.mlvm").read_bytes()
    assert (root / "main.s").read_text() == run("-S", source).decode()
    assert run("-i", "--emit-ir", source) == run("--emit-ir", source)
    run("-i", "-s", source, status=2)
    run("-c", source, env={"CEPHYR_LD": missing["CEPHYR_LD"]})
    obj = root / "main.o"
    assert obj.read_bytes()[:4] == b"\x7fELF" and obj.read_bytes()[16:18] == b"\x01\x00"
    run(source, helper, "-o", root / "default")
    execute(root / "default")
    run("--assembler=mlvm", "--linker", "mlvm", source, helper, "-o", root / "override", env=missing)
    execute(root / "override")

    # Executable names containing spaces/metacharacters are passed without a shell.
    log = root / "calls.jsonl"
    def wrapper(name, tool):
        path = root / name
        path.write_text(f"#!{sys.executable}\nimport json, os, sys\n"
                        f"with open({str(log)!r}, 'a') as f: f.write(json.dumps(sys.argv)+'\\n')\n"
                        f"os.execv({str(tool)!r}, [{str(tool)!r}]+sys.argv[1:])\n")
        path.chmod(0o755)
        return path

    assembler = wrapper("assembler ; spaced", bin_dir / "mlvm-as")
    linker = wrapper("linker ; spaced", bin_dir / "mlvm-ld")
    selected = {"CEPHYR_AS": str(assembler), "CEPHYR_LD": str(linker)}
    run(source, helper, "-o", root / "selected", env=selected)
    execute(root / "selected")
    calls = [json.loads(line) for line in log.read_text().splitlines()]
    assert [Path(call[0]).name for call in calls] == [assembler.name, assembler.name, linker.name]
    assert Path(calls[-1][-1]).name == "libc.a"
    assert Path(calls[-1][5]).name == "crt0.o"
    assert all(not Path(call[2]).exists() for call in calls)  # temporary outputs cleaned
    run("--assembler", assembler, "-c", helper, "-o", root / "helper.o", env=missing)
    run("--linker", linker, obj, root / "helper.o", "-o", root / "objects")
    execute(root / "objects")

    preserved = root / "existing"
    preserved.write_bytes(b"keep me")
    run("-c", source, "-o", preserved, status=127, env=missing)
    run(source, helper, "-o", preserved, status=127, env={"CEPHYR_LD": missing["CEPHYR_LD"]})
    fail = root / "fail"
    fail.write_text("#!/bin/sh\nexit 19\n")
    fail.chmod(0o755)
    run("--assembler", fail, "-c", source, "-o", preserved, status=19)
    run("--linker", fail, source, helper, "-o", preserved, status=19)
    assert preserved.read_bytes() == b"keep me"
    before = source.read_bytes()
    run(source, helper, "--linker", linker, "-o", source, status=2)
    assert source.read_bytes() == before
    run("--assembler=", "-c", source, status=2)
    run("--linker=", source, status=2)

    # GNU-compatible tools exercise the actual generated assembly and ELF ABI.
    if shutil.which("as") and shutil.which("ld"):
        for as_command, ld_command in [("as", "mlvm"), ("mlvm", "ld"), ("as", "ld")]:
            run("--assembler", as_command, "--linker", ld_command,
                source, helper, "-o", root / "system")
            execute(root / "system")
