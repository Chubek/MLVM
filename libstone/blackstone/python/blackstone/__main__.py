import argparse
from dataclasses import asdict
import json
from pathlib import Path
import sys
from .frontend import Diagnostic, load, ast_json
from .resolve import resolve
from .hir import load_fixture, dump_fixture
from .rewrite import Driver, Nonconvergence


def main():
    cli = argparse.ArgumentParser(description="Blackstone v0 guarded HIR rewrites (dparser frontend)")
    cli.add_argument("command", choices=("parse", "check", "apply"))
    cli.add_argument("kernel", type=Path)
    cli.add_argument("hir", nargs="?", type=Path)
    cli.add_argument("--kernel-dir", action="append", type=Path, default=[])
    cli.add_argument("--trace", action="store_true")
    cli.add_argument("--budget", type=int, default=10000)
    cli.add_argument("--engine", choices=("python", "native"), default="python",
                     help="rewrite driver for apply (native uses libblackstone)")
    args = cli.parse_args()
    try:
        kernels = {}
        directories = args.kernel_dir + ([args.kernel] if args.command == "check" and args.kernel.is_dir() else [])
        for directory in directories:
            for path in sorted(directory.rglob("*.bst")):
                item = load(path)
                if item.name in kernels:
                    item.span.fail(f"duplicate kernel {item.name}")
                kernels[item.name] = item
        if args.command == "check" and args.kernel.is_dir():
            if not kernels:
                raise Diagnostic("no .bst kernels found")
            for kernel in kernels.values():
                resolve(kernel, kernels)
            print(f"{len(kernels)} kernels, {sum(len(k.rules) for k in kernels.values())} local rules; valid")
            return 0
        kernel = load(args.kernel)
        if args.command == "parse":
            print(ast_json(kernel))
            return 0
        program = resolve(kernel, kernels)
        if args.command == "check":
            print(f"{kernel.name}: {len(kernel.rules)} local rules, {len(program.rules)} resolved rules; valid")
            return 0
        if args.hir is None:
            raise Diagnostic("apply requires a typed HIR JSON fixture")
        driver_class = Driver
        if args.engine == "native":
            from .native import Driver as driver_class
        driver = driver_class(program, budget=args.budget)
        try:
            result = driver.optimize(load_fixture(json.loads(args.hir.read_text())))
        finally:
            if args.engine == "native":
                driver.close()
        print(json.dumps(dump_fixture(result), indent=2))
        if args.trace:
            for event in driver.trace:
                print(json.dumps(asdict(event)), file=sys.stderr)
        return 0
    except (Diagnostic, OSError, json.JSONDecodeError) as error:
        if args.trace and isinstance(error, Nonconvergence):
            for event in error.trace:
                print(json.dumps(asdict(event)), file=sys.stderr)
        print(f"blackstone: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
