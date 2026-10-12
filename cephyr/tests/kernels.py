"""Blackstone and user-authored kernels applied to Cephyr's lowered MLVM IR."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

tool = Path(sys.argv[1])
root = Path(__file__).resolve().parents[2]


def run(*args, status=0):
    result = subprocess.run([str(tool), *map(str, args)], capture_output=True, timeout=15)
    assert result.returncode == status, result.stderr
    return result.stdout


with tempfile.TemporaryDirectory(prefix="cephyr-kernel-") as directory:
    directory = Path(directory)
    source = directory / "identity.c"
    source.write_text("int identity(int x) { return x + 0; }")
    assert b"arith.addi" in run("--emit-ir", source)
    builtin = "blackstone.hir.algebraic-simplification.add-zero"
    result = run("--emit-ir", f"--apply-kernel={builtin}", source)
    assert b"arith.addi" not in result and b"c.ptrload" in result
    custom = directory / "identity.olisp"
    definition = (root / "libstone/blackstone/kernels/hir/algebraic-simplification/add-zero.olisp").read_text()
    custom_id = "blackstone.hir.custom.cephyr-identity"
    custom.write_text(definition.replace(builtin, custom_id))
    options = [f"--kernel-file={custom}", f"--apply-kernel={custom_id}"]
    assert b"arith.addi" not in run("--emit-ir", *options, source)
    run("-c", *options, source, "-o", directory / "identity.o")
    run("-E", *options, source, status=2)
    run("--emit-ir", "--apply-kernel=missing", source, status=1)
    before = custom.read_bytes()
    run("--emit-ir", *options, source, "-o", custom, status=2)
    assert custom.read_bytes() == before
    assert b"arith.addi" not in run("-i", f"-f{builtin}", source)
    assert b"arith.addi" not in run("-i", "-f", custom, source)
    assert b"arith.addi" not in run("-i", f"-f{custom}", f"-f{custom}", source)
    plan = directory / "optimize.json"
    plan.write_text(json.dumps({"kernels": [builtin, {"file": custom.name}]}))
    assert b"arith.addi" not in run("-i", "--optimize", plan, source)
    run("-c", f"--optimize={plan}", source, "-o", directory / "planned.o")
    run("-E", "--optimize", plan, source, status=2)
    plan_before = plan.read_bytes()
    run("-i", "--optimize", plan, source, "-o", plan, status=2)
    run("-i", "--optimize", plan, source, "-o", custom, status=2)
    assert plan.read_bytes() == plan_before and custom.read_bytes() == before
    plan.write_text(json.dumps({"kernel_files": [custom.name], "kernels": [custom_id]}))
    assert b"arith.addi" not in run("-i", "--optimize", plan, source)
    run("-i", "-fmissing-kernel", source, status=1)
    run("-i", "-fmissing.olisp", source, status=3)
    # Schema errors and malformed JSON fail explicitly, before producing output.
    for malformed in ["[]", "{}", '{"kernels":null}', '{"kernels":42}',
                      '{"kernels":[],"kernels":[]}', '{"kernels":["x",]}',
                      '{"kernels":[],"typo":[]}', '{"kernels":[{"file":""}]}',
                      '{"kernels":[{"id":"x"}]}', '{"kernels":["\\u0000"]}',
                      '{"kernels":["\\ud800"]}', '{"kernels":[]} trailing',
                      '[' * 40 + ']' * 40]:
        plan.write_text(malformed)
        run("-i", "--optimize", plan, source, status=2)
    # JSON Unicode escape handling for paths (including surrogate pairs).
    unicode_kernel = directory / "custom-λ-😀.olisp"
    unicode_kernel.write_bytes(before)
    plan.write_text(json.dumps({"kernels": [{"file": unicode_kernel.name}]}))
    assert b"arith.addi" not in run("-i", "--optimize", plan, source)
