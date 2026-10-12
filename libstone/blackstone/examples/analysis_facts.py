"""Supply a host-proven nonzero fact for one stable input value."""
import argparse
import json
from pathlib import Path

from blackstone.frontend import load
from blackstone.hir import Type, Value, TreeAdapter, binary, dump_fixture
from blackstone.resolve import resolve
from blackstone.rewrite import Driver, describe


class NonzeroFacts(TreeAdapter):
    def __init__(self, proven_values):
        # The host's analysis has restricted these exact inputs to nonzero values.
        # This example explicitly supplies that input-domain assumption.
        self.proven_values = tuple(proven_values)

    def predicate(self, name, root, captures, skeleton, registry):
        if name == "Nonzero" and any(captures[0] is v for v in self.proven_values):
            return True
        return super().predicate(name, root, captures, skeleton, registry)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--engine", choices=("python", "native"), default="python")
    args = cli.parse_args()
    driver_class = Driver
    if args.engine == "native":
        from blackstone.native import Driver as driver_class
    root_dir = Path(__file__).resolve().parents[1]
    program = resolve(load(root_dir / "kernels/expressions/DivideSelf.bst"))
    type_ = Type(width=8)
    x = Value("input", type_, "x", pure=True, total=True)
    root = binary("divide", x, x, provenance="analysis:1")
    unknown = driver_class(program)
    proven = driver_class(program, NonzeroFacts([x]))
    try:
        before_proof = unknown.optimize(root)
        after_proof = proven.optimize(root)
        if before_proof is not root or after_proof.kind != "literal" or after_proof.value != 1:
            raise RuntimeError("the rewrite must require the supplied nonzero proof")
        print(json.dumps({"without_proof": describe(before_proof),
                          "with_proof": describe(after_proof),
                          "rules": [e.rule for e in proven.trace],
                          "hir": dump_fixture(after_proof)}, indent=2))
    finally:
        for driver in (unknown, proven):
            if hasattr(driver, "close"):
                driver.close()


if __name__ == "__main__":
    main()
