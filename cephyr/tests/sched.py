"""Cephyr scheduler actions: C sources in pipelines and C-target emission."""
from pathlib import Path
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1]).resolve()


def run(tool, *args, status=0, cwd=None):
    result = subprocess.run([str(bin_dir / tool), *map(str, args)],
                            capture_output=True, timeout=60, cwd=cwd)
    assert result.returncode == status, (tool, args, result.returncode, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="cephyr-sched-") as directory:
    root = Path(directory)
    (root / "answer.c").write_text("int answer(void) { int x = 40; return x + 2; }\n")
    pipeline = root / "pipeline.sched"
    pipeline.write_text(
        "local p = require(\"lsched\").pipeline()\n"
        "local src = p:cephyr(\"src\", {path = \"answer.c\"})\n"
        "local obj = p:target(\"obj\", src, {triple = \"x86_64-unknown-linux-gnu\"})\n"
        "p:text(\"ir\", src)\n"
        "return p\n"
    )
    unsupported = root / "unsupported.sched"
    unsupported.write_text(
        "local p = require(\"lsched\").pipeline()\n"
        "local src = p:cephyr(\"src\", {path = \"answer.c\"})\n"
        "p:target(\"wasm\", src, {triple = \"wasm32-unknown-unknown\"})\n"
        "return p\n"
    )

    run("mlvm-sched", pipeline, "--check", cwd=root)
    assert b"cephyr" in run("mlvm-sched", pipeline, "--dot", cwd=root).stdout
    ir = run("mlvm-sched", pipeline, "--result=ir", cwd=root).stdout
    assert b"builtin.func" in ir and b"answer" in ir
    # `mlvm --schedule` is an equivalent entry point.
    assert run("mlvm", f"--schedule={pipeline}", "--result=ir", cwd=root).stdout == ir

    obj = root / "answer.o"
    run("mlvm-sched", pipeline, "--result=obj", "-o", obj, cwd=root)
    assert obj.read_bytes()[:4] == b"\x7fELF"
    assert b"answer" in run("mlvm-nm", obj, cwd=root).stdout
    # C-dialect modules have no WebAssembly emission.
    run("mlvm-sched", unsupported, cwd=root, status=1)

    # Preprocessor options flow through the action.
    (root / "inc").mkdir()
    (root / "inc" / "vals.h").write_text("int bonus(void) { return 2; }\n")
    (root / "config.c").write_text(
        "#include \"vals.h\"\n"
        "#if VAL == 42\n"
        "int answer(void) { return VAL + bonus(); }\n"
        "#else\n"
        "#error nope\n"
        "#endif\n"
    )
    options = root / "options.sched"
    options.write_text(
        "local p = require(\"lsched\").pipeline()\n"
        "local src = p:cephyr(\"src\", {path = \"config.c\", includes = \"inc\", defines = \"VAL=42\"})\n"
        "p:text(\"ir\", src)\n"
        "return p\n"
    )
    configured = run("mlvm-sched", options, "--result=ir", cwd=root).stdout
    assert b"answer" in configured and b"bonus" in configured

    # A failing node publishes nothing and creates no output.
    (root / "bad.c").write_text("int f(void) { return missing; }\n")
    failing = root / "failing.sched"
    failing.write_text(
        "local p = require(\"lsched\").pipeline()\n"
        "local src = p:cephyr(\"src\", {path = \"bad.c\"})\n"
        "p:text(\"ir\", src)\n"
        "return p\n"
    )
    missing = root / "missing.mlvm"
    run("mlvm-sched", failing, "--result=ir", "-o", missing, cwd=root, status=1)
    assert not missing.exists()
    # Unknown options fail at load time.
    typo = root / "typo.sched"
    typo.write_text(
        "local p = require(\"lsched\").pipeline()\n"
        "p:cephyr(\"src\", {path = \"answer.c\", typo = \"x\"})\n"
        "return p\n"
    )
    run("mlvm-sched", typo, "--check", cwd=root, status=1)
