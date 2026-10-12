"""Construct typed HIR and optimize it using either rewrite engine."""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

from blackstone.frontend import load
from blackstone.hir import Type, Value, binary, literal, dump_fixture
from blackstone.resolve import resolve
from blackstone.rewrite import Driver


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--engine", choices=("python", "native"), default="python")
    args = cli.parse_args()
    driver_class = Driver
    if args.engine == "native":
        from blackstone.native import Driver as driver_class
    root_dir = Path(__file__).resolve().parents[1]
    program = resolve(load(root_dir / "kernels/expressions/AlgebraicSimplification.bst"))
    type_ = Type(width=8)
    x = Value("input", type_, "x", pure=True, total=True, provenance="api:x")
    root = binary("plus", binary("plus", x, literal(250, type_)), literal(6, type_), provenance="api:sum")
    driver = driver_class(program)
    try:
        result = driver.optimize(root)
        if result is not x:
            raise RuntimeError("the identity cleanup must preserve the original x")
        print(json.dumps({"trace": [asdict(event) for event in driver.trace], "hir": dump_fixture(result)}, indent=2))
    finally:
        if hasattr(driver, "close"):
            driver.close()


if __name__ == "__main__":
    main()
