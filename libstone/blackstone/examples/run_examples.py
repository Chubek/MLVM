"""Run the documented cases, checking exact types, rule order and fixed points.

From Blackstone: PYTHONPATH=python python3 examples/run_examples.py --engine both
Parser/library locations use BLACKSTONE_PARSER and BLACKSTONE_LIBRARY as usual.
"""
import argparse
import json
from pathlib import Path
import sys

from blackstone.frontend import Diagnostic, load
from blackstone.hir import load_fixture, dump_fixture
from blackstone.resolve import resolve
from blackstone.rewrite import Driver, Nonconvergence, describe

ROOT = Path(__file__).resolve().parents[1]


def check(condition, message):
    if not condition:
        raise Diagnostic(message)


def run_case(case, driver_class, programs):
    key = (case["kernel"], tuple(case.get("kernel_dirs", [])))
    if key not in programs:
        kernels = {}
        for directory in key[1]:
            for path in sorted((ROOT / directory).rglob("*.bst")):
                kernel = load(path)
                check(kernel.name not in kernels, f"duplicate example kernel {kernel.name}")
                kernels[kernel.name] = kernel
        programs[key] = resolve(load(ROOT / key[0]), kernels)
    root = load_fixture(json.loads((ROOT / case["input"]).read_text()))
    before = dump_fixture(root)
    driver = driver_class(programs[key], budget=case.get("budget", 10000))
    try:
        try:
            result = driver.optimize(root)
        except Nonconvergence as error:
            check("error" in case and case["error"] in str(error), f"unexpected nonconvergence: {error}")
            check([e.rule for e in error.trace] == case["rules"], "unexpected failure trace")
            check(dump_fixture(root) == before, "failed optimization changed its input")
            return case["error"]
        check("error" not in case, "expected budget exhaustion")
        check(describe(result) == case["result"], f"expected {case['result']}, got {describe(result)}")
        check(result.type == root.type, "replacement changed the exact result type")
        check([e.rule for e in driver.trace] == case["rules"], "unexpected rule order")
        if case.get("unchanged"):
            check(result is root, "a nonmatching example changed node identity")
        if "origin" in case:
            check(result.origin == case["origin"], "unexpected originating rule")
        check(dump_fixture(root) == before, "optimization mutated its input")
        check(driver.optimize(result) is result and driver.trace == [], "result is not an identity-preserving fixed point")
        return describe(result)
    finally:
        if hasattr(driver, "close"):
            driver.close()


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--engine", choices=("python", "native", "both"), default="both")
    cli.add_argument("--case", action="append", default=[], help="run a named case; repeat to select more")
    args = cli.parse_args()
    try:
        cases = json.loads((ROOT / "examples/cases.json").read_text())
        names = {case["name"] for case in cases}
        check(len(names) == len(cases), "duplicate example case name")
        check(not set(args.case) - names, "unknown case: " + ", ".join(sorted(set(args.case) - names)))
        selected = [c for c in cases if not args.case or c["name"] in args.case]
        engines = [("python", Driver)] if args.engine != "native" else []
        if args.engine != "python":
            from blackstone.native import Driver as NativeDriver
            engines.append(("native", NativeDriver))
        programs = {}
        for engine, driver_class in engines:
            for case in selected:
                try:
                    result = run_case(case, driver_class, programs)
                except Diagnostic as error:
                    raise Diagnostic(f"{engine}/{case['name']}: {error}") from error
                print(f"{engine}/{case['name']}: {result}")
        print(f"{len(selected)} cases passed on {len(engines)} engine(s)")
        return 0
    except (Diagnostic, OSError, json.JSONDecodeError) as error:
        print(f"examples: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
