from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import unittest

from blackstone.frontend import Diagnostic, load, parse, ast_json
from blackstone.resolve import Registry, resolve, decimal
from blackstone.hir import Type, Value, binary, literal, load_fixture, dump_fixture, TreeAdapter
from blackstone.rewrite import Driver, Nonconvergence, describe

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "kernels/expressions/AlgebraicSimplification.bst"
SOURCE = FIXTURE.read_text()


def reference(node, inputs, effects=None):
    """Independent occurrence evaluator: bit representations and truncating division."""
    type = node.type
    mask = (1 << type.width) - 1
    if node.kind == "input":
        if effects is not None and node.pure is not True:
            effects.append(node.name)
        return inputs[node.name]
    if node.kind == "literal":
        return node.value
    a, b = [reference(c, inputs, effects) for c in node.children]
    op = node.name
    if op == "plus": result = a + b
    elif op == "minus": result = a - b
    elif op == "times": result = a * b
    elif op == "bit_and": result = (a & mask) & (b & mask)
    elif op == "bit_or": result = (a & mask) | (b & mask)
    elif op == "bit_xor": result = (a & mask) ^ (b & mask)
    elif op == "divide":
        if b == 0 or (type.signed and a == -(1 << (type.width - 1)) and b == -1):
            raise ArithmeticError("exceptional division")
        result = abs(a) // abs(b)
        if (a < 0) != (b < 0): result = -result
    result &= mask
    if type.signed and result & (1 << (type.width - 1)):
        result -= mask + 1
    return result


def examples(type, x):
    zero = literal(0, type)
    c = literal(-1 if type.signed else (1 << type.width) - 1, type)
    result = {
        "AddZero": binary("plus", zero, x),
        "AddZeroRight": binary("plus", x, zero),
        "SubtractZero": binary("minus", x, zero),
        "SubtractSelf": binary("minus", x, x),
        "MultiplyZero": binary("times", zero, x),
        "MultiplyZeroRight": binary("times", x, zero),
        "AndZero": binary("bit_and", x, zero),
        "AndSelf": binary("bit_and", x, x),
        "OrZero": binary("bit_or", x, zero),
        "XorSelf": binary("bit_xor", x, x),
        "AddConstantRight": binary("plus", c, x),
        "ReassociateAddConstants": binary("plus", binary("plus", x, c), c),
        "ReassociateMultiplyConstants": binary("times", binary("times", x, c), c),
    }
    if type.represents(1):
        one = literal(1, type)
        result.update(MultiplyOne=binary("times", one, x), MultiplyOneRight=binary("times", x, one),
                      DivideOne=binary("divide", x, one))
    return result


class FrontendTests(unittest.TestCase):
    def test_fixture_ast(self):
        kernel = load(FIXTURE)
        self.assertEqual(len(kernel.rules), 16)
        self.assertEqual(len(resolve(kernel).rules), 17)
        self.assertEqual(kernel.rules[-1].pattern.children[0].sort, "Binary")
        self.assertEqual(kernel.rules[3].pattern.children[1].argument, "$1")
        self.assertEqual(kernel.rules[0].span.line, 29)
        self.assertEqual(json.loads(ast_json(kernel))["name"], "AlgebraicSimplification")

    def test_comments_groups_precedence_and_large_integers(self):
        modified = SOURCE.replace("Term!exact(0)", "Term /* signature */ !exact(- /* number */ 123456789012345678901234567890)", 1)
        modified = modified.replace("Associative && ModularInteger", "(Associative && (ModularInteger))", 1)
        kernel = parse(modified)
        resolve(kernel)
        self.assertEqual(kernel.rules[0].pattern.children[0].argument, "-123456789012345678901234567890")
        self.assertEqual(kernel.requires[0].name, "Relational::Properties::Associativity")
        self.assertEqual(kernel.requires[1].name, "Relational::Properties::Commutativity")
        long = "9" * 5000
        large = parse(SOURCE.replace("exact(0)", f"exact({long})", 1))
        self.assertEqual(large.rules[0].pattern.children[0].argument, long)
        self.assertGreater(decimal(long), 1 << 10000)
        wide = literal(decimal(long), Type(width=20000))
        self.assertEqual(dump_fixture(wide)["nodes"]["n0"]["value"], long)
        self.assertEqual(describe(wide), long)
        # An unparenthesized chain must build a left-associated tree.
        chain = parse(SOURCE.replace("$1 `plus` $2 ~> $2", "$1 `plus` $2 `times` $1 ~> $2", 1))
        self.assertEqual(chain.rules[0].match.value, "times")
        self.assertEqual(chain.rules[0].match.children[0].value, "plus")

    def test_negative_diagnostics(self):
        cases = [
            ("$1 `plus` $2", "$1 `unknown` $2", "unknown operator"),
            ("where Associative", "where Imaginary", "predicate"),
            ("-> Term", "-> Bogus", "sort"),
            ("!resolves", "!bogus", "refinement"),
            ("HIR::Effects::{Pure, Total}", "HIR::Effects::{Pure, Missing}", "capability"),
            ("HIR::Literals::Term", "HIR::Literals::Bogus", "HIR view"),
            ("$1 `plus` $2 ~> $2", "$0 `plus` $2 ~> $2", "out of range"),
            ("$1 `plus` $2 ~> $2", "$1 `plus` $2 ~> $3", "out of range"),
            ("same($1)", "same($2)", "earlier"),
            ("$1 `plus` $2 ~> $2", "$2 `plus` $1 ~> $2", "tree mismatch"),
            ("($1 `plus` $2) `plus` $3", "$1 `plus` ($2 `plus` $3)", "tree mismatch"),
            ("Pure($1)", "Pure($1, $2)", "expects 1"),
            ("!resolves", "!resolves(1)", "no argument"),
            ("AddZeroRight ::", "AddZero ::", "duplicate rule"),
            ("inherits ConstantFolding", "inherits Unknown", "unknown inherited"),
            ("inherits ConstantFolding", "inherits AlgebraicSimplification", "cycle"),
            ("Kernel AlgebraicSimplification(inherits", "KernelAlgebraicSimplification(inherits", "syntax error"),
            ("Binary(Term!exact(0), Term!resolves)", "Binary(Term!exact(0) Term!resolves)", "syntax error"),
            ("$1 `plus` $2", "$1 `plus $2", "unterminated backtick"),
        ]
        for before, after, expected in cases:
            with self.subTest(after=after):
                with self.assertRaises(Diagnostic) as caught:
                    resolve(parse(SOURCE.replace(before, after, 1), "bad.bst"))
                self.assertIn(expected, str(caught.exception))
                self.assertRegex(str(caught.exception), r"bad.bst:\d+:\d+:")
        for suffix in ("/* missing", "\0"):
            with self.assertRaises(Diagnostic):
                parse(SOURCE + suffix)
        # Comments do not nest: the first closing delimiter ends the comment.
        resolve(parse("/* outer /* inner */\n" + SOURCE))
        with self.assertRaises(Diagnostic):
            parse("# not a comment\n" + SOURCE)
        with self.assertRaises(Diagnostic):
            parse("Kernel A; concerns X; Kernel B;")

    def test_inheritance_order_diamond_duplicates_and_cycle(self):
        def kernel(name, bases=""):
            return parse(f"Kernel {name}{'(inherits ' + bases + ')' if bases else ''};")
        a, b = kernel("A", "ConstantFolding"), kernel("B", "ConstantFolding")
        c = kernel("C", "A, B")
        self.assertEqual([r.name for r in resolve(c, {"A": a, "B": b}).rules], ["ConstantFolding"])
        a = kernel("A", "B")
        b = kernel("B", "A")
        with self.assertRaisesRegex(Diagnostic, "A -> B -> A"):
            resolve(a, {"B": b})
        duplicate = parse(SOURCE.replace("AlgebraicSimplification(inherits ConstantFolding)", "Base"))
        derived = parse(SOURCE.replace("inherits ConstantFolding", "inherits Base"))
        with self.assertRaisesRegex(Diagnostic, "duplicate rule"):
            resolve(derived, {"Base": duplicate})
        registry = Registry()
        registry.capabilities["Other::Pure"] = ("Pure", 1)
        with self.assertRaisesRegex(Diagnostic, "ambiguous"):
            resolve(parse(SOURCE.replace("requires HIR::Effects", "requires Other::Pure; requires HIR::Effects")), registry=registry)


class SemanticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = resolve(load(FIXTURE))

    def test_all_rules_exhaustive_small_widths(self):
        exercised = set()
        for width in range(1, 5):
            for signed in (False, True):
                t = Type(width=width, signed=signed)
                x = Value("input", t, "x", pure=True, total=True)
                for name, node in examples(t, x).items():
                    driver = Driver(self.program)
                    result = driver.optimize(node)
                    # Very narrow constants can already be identities, taking priority.
                    if name not in ("ReassociateMultiplyConstants",) or width > 1 or signed:
                        self.assertIn(name, [event.rule for event in driver.trace], (name, width, signed))
                    exercised.update(event.rule for event in driver.trace if event.rule != "ConstantFolding")
                    self.assertEqual(result.type, t)
                    for bits in range(1 << width):
                        number = bits - (1 << width) if signed and bits >= 1 << (width - 1) else bits
                        self.assertEqual(reference(node, {"x": number}), reference(result, {"x": number}), (name, t, number))
                    self.assertIs(driver.optimize(result), result)
                    self.assertEqual(driver.trace, [])
        self.assertEqual(exercised, {r.name for r in self.program.kernel.rules})

    def test_folding_exhaustive_and_exceptional_division(self):
        for width in range(1, 5):
            for signed in (False, True):
                t = Type(width=width, signed=signed)
                values = range(-(1 << (width - 1)), 1 << (width - 1)) if signed else range(1 << width)
                for op in self.program.registry.operators:
                    if self.program.registry.operators[op].domain != "integer":
                        continue
                    for a in values:
                        for b in values:
                            node = binary(op, literal(a, t), literal(b, t))
                            result = Driver(self.program).optimize(node)
                            try:
                                expected = reference(node, {})
                            except ArithmeticError:
                                self.assertIs(result, node)
                            else:
                                self.assertEqual(result.kind, "literal")
                                self.assertEqual(result.type, t)
                                self.assertEqual(result.value, expected)

    def test_nonmatches_types_flags_and_missing_facts(self):
        t = Type(width=8)
        x = Value("input", t, "x", pure=True, total=True)
        zero = literal(0, t)
        cases = [binary("plus", x, literal(0, Type(width=16))),
                 binary("plus", x, literal(0, Type(width=8, signed=True)))]
        for domain in ("float", "pointer", "boolean", "vector"):
            type_ = Type(domain, 8)
            z = Value("literal", type_, value=0)
            cases.append(binary("plus", Value("input", type_, "x"), z))
        for flag in ("nsw", "nuw", "saturating", "checked", "exact", "unknown"):
            cases.append(binary("plus", x, zero, flags=(flag,)))
            cases.append(binary("plus", binary("plus", x, literal(2, t), flags=(flag,)), literal(3, t)))
        cases.append(binary("plus", replace(x, resolved=False), zero))
        for pure, total in ((None, True), (True, None), (False, True), (True, False), (None, None)):
            unknown = replace(x, pure=pure, total=total)
            cases.extend([binary("times", zero, unknown), binary("minus", unknown, unknown),
                          binary("bit_and", unknown, zero), binary("bit_xor", unknown, unknown)])
        for node in cases:
            with self.subTest(node=describe(node)):
                driver = Driver(self.program)
                self.assertIs(driver.optimize(node), node)
                self.assertEqual(driver.trace, [])
        s1 = Type(width=1, signed=True)
        x1 = Value("input", s1, "x", pure=True, total=True)
        for op in ("times", "divide"):
            node = binary(op, x1, literal(-1, s1))
            self.assertIs(Driver(self.program).optimize(node), node)

    def test_effects_traps_divergence_poison_undef_and_distinct_identity(self):
        t = Type(width=8)
        zero = literal(0, t)
        for name, pure, total in (("effectful", False, True), ("trap", True, False), ("diverge", True, False),
                                  ("poison", True, False), ("undef", False, False), ("volatile", False, True)):
            x = Value("input", t, name, pure=pure, total=total)
            for op in ("minus", "bit_and", "bit_xor"):
                node = binary(op, x, x)
                self.assertIs(Driver(self.program).optimize(node), node)
            for op in ("times", "bit_and"):
                node = binary(op, x, zero)
                self.assertIs(Driver(self.program).optimize(node), node)
        x = Value("input", t, "x", pure=True, total=True)
        y = replace(x)
        for op in ("minus", "bit_and", "bit_xor"):
            node = binary(op, x, y)
            self.assertIs(Driver(self.program).optimize(node), node)
        # Identical-looking binary subtrees must also remain identity-distinct.
        a, b = binary("plus", x, literal(2, t)), binary("plus", x, literal(2, t))
        node = binary("minus", a, b)
        self.assertIs(Driver(self.program).optimize(node), node)

    def test_effectful_retention_movement_and_order(self):
        t = Type(width=8)
        x = Value("input", t, "x", pure=False, total=False)
        y = Value("input", t, "y", pure=False, total=True)
        for name, node in examples(t, x).items():
            if name in ("SubtractSelf", "MultiplyZero", "MultiplyZeroRight", "AndZero", "AndSelf", "XorSelf"):
                continue
            whole = binary("plus", node, y)
            driver = Driver(self.program)
            result = driver.optimize(whole)
            self.assertIn(name, [e.rule for e in driver.trace])
            before, after = [], []
            self.assertEqual(reference(whole, {"x": 13, "y": 9}, before), reference(result, {"x": 13, "y": 9}, after))
            self.assertEqual(before, ["x", "y"])
            self.assertEqual(after, before)

    def test_guards_short_circuit_and_no_construction_before_proof(self):
        class Spy(TreeAdapter):
            def __init__(self): self.queries, self.builds = [], []
            def predicate(self, name, *args):
                self.queries.append(name)
                return None
            def literal(self, *args):
                self.builds.append(args)
                return super().literal(*args)
        spy = Spy()
        t = Type(width=8)
        x = Value("input", t, "x", pure=True, total=True)
        node = binary("minus", x, x)
        Driver(self.program, spy).optimize(node)
        self.assertEqual(spy.queries, ["ModularInteger"])
        self.assertEqual(spy.builds, [])

    def test_shared_identity_survives_child_normalization(self):
        t = Type(width=8)
        x = Value("input", t, "x", pure=True, total=True)
        shared = binary("plus", literal(2, t), x)
        node = binary("minus", shared, shared)
        driver = Driver(self.program)
        result = driver.optimize(node)
        self.assertEqual(result.kind, "literal")
        self.assertEqual(result.value, 0)
        self.assertEqual(result.type, t)
        self.assertEqual([e.rule for e in driver.trace], ["AddConstantRight", "SubtractSelf"])
        self.assertIs(node.children[0], node.children[1])

    def test_inherited_and_source_order_first_match(self):
        declarations = "concerns HIR::Literals::Term; concerns HIR::Expressions::Binary; requires Arithmetic::Domains::ModularInteger;"
        def rule(name):
            return f"{name} :: Binary(Term!resolves, Term!exact(0)) -> Term where ModularInteger {{$1 `plus` $2 ~> $1;}}"
        base = parse("Kernel Base;" + declarations + rule("BaseFirst") + rule("BaseSecond"))
        local = parse("Kernel Local(inherits Base);" + declarations + rule("LocalRule"))
        program = resolve(local, {"Base": base})
        self.assertEqual([r.name for r in program.rules], ["BaseFirst", "BaseSecond", "LocalRule"])
        t = Type(width=8)
        x = Value("input", t, "x")
        driver = Driver(program)
        self.assertIs(driver.optimize(binary("plus", x, literal(0, t))), x)
        self.assertEqual([e.rule for e in driver.trace], ["BaseFirst"])

    def test_fixed_point_wraparound_determinism_provenance(self):
        root = load_fixture(json.loads((ROOT / "examples/reassociate.json").read_text()))
        first, second = Driver(self.program), Driver(self.program)
        result = first.optimize(root)
        second.optimize(root)
        self.assertEqual(first.trace, second.trace)
        self.assertEqual([e.rule for e in first.trace], ["ReassociateAddConstants", "ConstantFolding"])
        self.assertEqual(result.children[1].value, 7)
        self.assertEqual(result.provenance, "demo:1")
        self.assertEqual(result.origin, "ReassociateAddConstants")
        self.assertEqual(result.children[1].origin, "ConstantFolding")
        self.assertEqual(describe(root), "((x plus 3) plus 4)")
        self.assertEqual(dump_fixture(load_fixture(dump_fixture(result))), dump_fixture(result))
        t = Type(width=3)
        x = Value("input", t, "x", pure=True, total=True)
        cleanup = binary("plus", binary("plus", x, literal(3, t)), literal(5, t))
        driver = Driver(self.program)
        self.assertIs(driver.optimize(cleanup), x)
        self.assertEqual([e.rule for e in driver.trace], ["ReassociateAddConstants", "ConstantFolding", "AddZeroRight"])

    def test_budget_nochange_and_cycles(self):
        rule = """Kernel Swap; concerns HIR::Literals::Term; concerns HIR::Expressions::Binary;
        requires Arithmetic::Domains::ModularInteger;
        Swap :: Binary(Term!resolves, Term!resolves) -> Term where ModularInteger
        {$1 `plus` $2 ~> $2 `plus` $1;}"""
        t = Type(width=8)
        a, b = Value("input", t, "a"), Value("input", t, "b")
        node = binary("plus", a, b)
        driver = Driver(resolve(parse(rule)), budget=4)
        with self.assertRaises(Nonconvergence) as caught:
            driver.optimize(node)
        self.assertEqual(len(caught.exception.trace), 4)
        self.assertIn("Swap", str(caught.exception))
        self.assertIs(node.children[0], a)
        nochange = Driver(resolve(parse(rule.replace("$2 `plus` $1", "$1 `plus` $2"))), budget=0)
        self.assertIs(nochange.optimize(node), node)
        cyclic = {"root": "a", "nodes": {"a": {"kind": "binary", "op": "plus", "args": ["a", "a"]}}}
        with self.assertRaisesRegex(Diagnostic, "cyclic"):
            load_fixture(cyclic)
        with self.assertRaisesRegex(Diagnostic, "budget"):
            Driver(self.program, budget=-1)


class CLITests(unittest.TestCase):
    def test_actual_fixture_end_to_end(self):
        def run(*args):
            return subprocess.run([sys.executable, "-m", "blackstone", *map(str, args)], capture_output=True, text=True)
        parsed = run("parse", FIXTURE)
        self.assertEqual(parsed.returncode, 0, parsed.stderr)
        self.assertEqual(len(json.loads(parsed.stdout)["rules"]), 16)
        checked = run("check", FIXTURE)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertIn("16 local rules", checked.stdout)
        applied = run("apply", FIXTURE, ROOT / "examples/reassociate.json", "--trace")
        self.assertEqual(applied.returncode, 0, applied.stderr)
        self.assertEqual(describe(load_fixture(json.loads(applied.stdout))), "(x plus 7)")
        self.assertEqual([json.loads(line)["rule"] for line in applied.stderr.splitlines()],
                         ["ReassociateAddConstants", "ConstantFolding"])
        failed = run("apply", FIXTURE, ROOT / "examples/reassociate.json", "--budget", "1", "--trace")
        self.assertEqual(failed.returncode, 1)
        self.assertIn("budget 1 exhausted", failed.stderr)
        self.assertEqual(failed.stdout, "")


if __name__ == "__main__":
    unittest.main()
