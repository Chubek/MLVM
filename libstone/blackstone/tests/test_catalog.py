"""Catalog acceptance: native parsing, integer semantics and host proof boundaries."""
from collections import Counter
from dataclasses import replace
import itertools
from pathlib import Path
import subprocess
import sys
import unittest

from blackstone.frontend import Diagnostic, load, parse
from blackstone.resolve import decimal, resolve
from blackstone.hir import Type, Value, TreeAdapter, binary, literal, modular
from blackstone.rewrite import Driver
from test_contract import reference

ROOT = Path(__file__).resolve().parents[1]
FILES = sorted((ROOT / "kernels").rglob("*.bst"))
COUNTS = {"expressions": 32, "statements": 12, "memory": 8, "control-flow": 12, "loops": 12, "polyhedra": 8}
DOMAINS = {"statements": "statement", "memory": "memory", "control-flow": "control", "loops": "loop", "polyhedra": "polyhedral"}


def specimen(rule, type):
    """Build well-typed inputs from the signature, preserving specified identities."""
    captures = []
    ones = {decimal(p.arguments[0]) for p in rule.guard if p.name == "AllOnes"}

    def visit(pattern, expr):
        if pattern.sort == "Binary":
            return binary(expr.value, *(visit(p, e) for p, e in zip(pattern.children, expr.children)))
        slot = len(captures) + 1
        if pattern.refinement == "same":
            value = captures[decimal(pattern.argument[1:]) - 1]
        elif pattern.refinement == "exact":
            value = literal(decimal(pattern.argument), type)
        elif pattern.refinement == "literal":
            value = literal(type.normalize(-1 if slot in ones else 3), type)
        else:
            value = Value("input", type, f"v{slot}", pure=True, total=True)
        captures.append(value)
        return value
    return visit(rule.pattern, rule.match), captures


class ProvenHost(TreeAdapter):
    """Test host only: exercise installation shapes, not a CFG/polyhedral solver."""
    def __init__(self, denied=None, answer=None):
        self.denied, self.answer, self.constructed = denied, answer, []

    def predicate(self, name, root, captures, skeleton, registry):
        if name == self.denied:
            return self.answer
        if name in ("Pure", "Total"):
            return super().predicate(name, root, captures, skeleton, registry)
        if name == "CanonicalStatementOrder":
            return root.children[0].name < root.children[1].name
        return True

    def binary(self, *args):
        self.constructed.append(args[0])
        return super().binary(*args)


class NonzeroInputs(TreeAdapter):
    def predicate(self, name, root, captures, skeleton, registry):
        if name == "Nonzero" and captures[0].kind == "input":
            return True  # Test input domain is restricted to nonzero values below.
        return super().predicate(name, root, captures, skeleton, registry)


class CatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.kernels = {path: load(path) for path in FILES}

    def test_exact_inventory_and_every_file_validates(self):
        additions = [p for p in FILES if p.name != "AlgebraicSimplification.bst"]
        self.assertEqual(len(FILES), 85)
        self.assertEqual(len(additions), 84)
        self.assertEqual(Counter(p.parent.name for p in additions), COUNTS)
        names = [kernel.name for kernel in self.kernels.values()]
        self.assertEqual(len(names), len(set(names)))
        local_names = []
        for path, kernel in self.kernels.items():
            with self.subTest(path=path):
                self.assertEqual(kernel.name, path.stem)
                self.assertGreater(len(kernel.rules), 0)
                resolve(kernel)
                local_names.extend(rule.name for rule in kernel.rules)
        self.assertEqual(len(local_names), len(set(local_names)))

    def test_integer_kernels_exhaustive_semantics(self):
        exercised = set()
        for path, kernel in self.kernels.items():
            if path.parent.name != "expressions":
                continue
            program = resolve(kernel)
            for width in (2, 3):
                for signed in (False, True):
                    type = Type(width=width, signed=signed)
                    try:
                        node, captures = specimen(kernel.rules[0], type)
                    except Diagnostic:  # e.g. exact(-1) in unsigned types.
                        continue
                    driver = Driver(program, NonzeroInputs())
                    result = driver.optimize(node)
                    if any(e.rule == kernel.rules[0].name for e in driver.trace):
                        exercised.add(kernel.name)
                    self.assertEqual(result.type, type)
                    values = range(-(1 << (width - 1)), 1 << (width - 1)) if signed else range(1 << width)
                    names = sorted({c.name for c in captures if c.kind == "input"})
                    nonzero = {captures[decimal(p.arguments[0]) - 1].name for p in kernel.rules[0].guard if p.name == "Nonzero"}
                    for values_tuple in itertools.product(values, repeat=len(names)):
                        inputs = dict(zip(names, values_tuple))
                        if any(inputs.get(name) == 0 for name in nonzero):
                            continue
                        with self.subTest(kernel=kernel.name, type=type, inputs=inputs):
                            self.assertEqual(reference(node, inputs), reference(result, inputs))
                    self.assertIs(driver.optimize(result), result)
                    self.assertEqual(driver.trace, [])
        self.assertEqual(exercised, {k.name for p, k in self.kernels.items() if p.parent.name == "expressions"})

    def test_host_kernels_unknown_denied_and_proven(self):
        for path, kernel in self.kernels.items():
            if path.parent.name not in DOMAINS:
                continue
            with self.subTest(kernel=kernel.name):
                domain = DOMAINS[path.parent.name]
                program = resolve(kernel)
                rule = kernel.rules[0]
                root, captures = specimen(rule, Type(domain=domain))
                before = root.children
                default = Driver(program)
                self.assertIs(default.optimize(root), root)
                self.assertEqual(default.trace, [])
                for guard in rule.guard:
                    for answer in (False, None):
                        adapter = ProvenHost(guard.name, answer)
                        driver = Driver(program, adapter)
                        self.assertIs(driver.optimize(root), root)
                        self.assertEqual(driver.trace, [])
                        self.assertEqual(adapter.constructed, [])
                adapter = ProvenHost()
                driver = Driver(program, adapter, budget=1)
                result = driver.optimize(root)
                self.assertEqual(result.type, root.type)
                self.assertEqual([event.rule for event in driver.trace], [rule.name])
                self.assertEqual(root.children, before)
                self.assertIs(driver.optimize(result), result)
                self.assertEqual(driver.trace, [])
                if rule.build.kind == "binary":
                    self.assertEqual(result.name, rule.build.value)
                    self.assertEqual(result.origin, rule.name)
                else:
                    self.assertIs(result, captures[decimal(rule.build.value) - 1])

    def test_host_operators_never_become_integer_folds(self):
        program = resolve(self.kernels[ROOT / "kernels/expressions/AlgebraicSimplification.bst"])
        for name, op in program.registry.operators.items():
            if op.domain == "integer":
                continue
            root = binary(name, literal(1), literal(2))
            self.assertFalse(modular([root], program.registry))
            self.assertIs(Driver(program).optimize(root), root)
        kernel = self.kernels[ROOT / "kernels/statements/SequenceEmptyLeft.bst"]
        root = binary("seq", literal(0), literal(1))
        self.assertIs(Driver(resolve(kernel), ProvenHost()).optimize(root), root)

    def test_expression_proof_failures_preserve_evaluations(self):
        class Denied(NonzeroInputs):
            def __init__(self, predicate, answer):
                self.denied, self.answer = predicate, answer

            def predicate(self, name, *args):
                if name == self.denied:
                    return self.answer
                return super().predicate(name, *args)

        for path, kernel in self.kernels.items():
            # This checks an individual rule's proof gate. The acceptance kernel
            # has other rules that can independently simplify the same input.
            if path.parent.name != "expressions" or kernel.name == "AlgebraicSimplification":
                continue
            node, _ = specimen(kernel.rules[0], Type(width=8, signed=True))
            for guard in kernel.rules[0].guard:
                for answer in (False, None):
                    with self.subTest(kernel=kernel.name, guard=guard.name, answer=answer):
                        driver = Driver(resolve(kernel), Denied(guard.name, answer))
                        self.assertIs(driver.optimize(node), node)
                        self.assertEqual(driver.trace, [])

    def test_host_flags_and_unresolved_inputs_cannot_be_certified(self):
        for path, kernel in self.kernels.items():
            if path.parent.name not in DOMAINS:
                continue
            root, _ = specimen(kernel.rules[0], Type(domain=DOMAINS[path.parent.name]))
            for altered in (replace(root, flags=("unknown_semantics",)), replace(root, resolved=False)):
                with self.subTest(kernel=kernel.name):
                    driver = Driver(resolve(kernel), ProvenHost())
                    self.assertIs(driver.optimize(altered), altered)
                    self.assertEqual(driver.trace, [])

    def test_host_rules_require_correct_proof_and_domain(self):
        path = ROOT / "kernels/loops/LoopUnrolling.bst"
        source = path.read_text()
        with self.assertRaisesRegex(Diagnostic, "host transformation proof"):
            resolve(parse(source.replace("where UnrollSafe", "where Pure($1)").replace(
                "requires Optimization::Loops::UnrollSafe;", "requires HIR::Effects::Pure;")))
        with self.assertRaisesRegex(Diagnostic, "carrier domains"):
            resolve(parse(source.replace("`unrolled_loop`", "`tiled_scop`")))

    def test_check_entire_catalog_cli(self):
        result = subprocess.run([sys.executable, "-B", "-m", "blackstone", "check", str(ROOT / "kernels")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("85 kernels, 100 local rules; valid", result.stdout)


if __name__ == "__main__":
    unittest.main()
