"""Run the semantic contract and host proof gates through the C matcher/driver."""
import ctypes as C
import json
import subprocess
import sys
import unittest
from unittest.mock import patch

import test_contract as contract
import test_catalog as catalog
from blackstone.frontend import Diagnostic, parse
from blackstone.hir import Type, Value, binary, literal
from blackstone.resolve import resolve
from blackstone.native import Driver, Error, Lowering, library


class NativeSemanticsTests(contract.SemanticsTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.swap = patch.object(contract, "Driver", Driver)
        cls.swap.start()

    @classmethod
    def tearDownClass(cls):
        cls.swap.stop()


class NativeCatalogTests(catalog.CatalogTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.swap = patch.object(catalog, "Driver", Driver)
        cls.swap.start()

    @classmethod
    def tearDownClass(cls):
        cls.swap.stop()


class NativeBoundaryTests(unittest.TestCase):
    def test_native_cli_and_atomic_budget_failure(self):
        command = [sys.executable, "-m", "blackstone", "apply", str(contract.FIXTURE),
                   str(contract.ROOT / "examples/reassociate.json"), "--engine", "native", "--trace"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(contract.describe(contract.load_fixture(json.loads(result.stdout))), "(x plus 7)")
        self.assertEqual([json.loads(line)["rule"] for line in result.stderr.splitlines()],
                         ["ReassociateAddConstants", "ConstantFolding"])
        failed = subprocess.run(command + ["--budget", "1"], capture_output=True, text=True)
        self.assertEqual(failed.returncode, 1)
        self.assertEqual(failed.stdout, "")
        self.assertIn("budget 1 exhausted", failed.stderr)

    def test_arbitrary_width_exact_and_folding(self):
        program = resolve(contract.load(contract.FIXTURE))
        t = Type(width=20000, signed=True)
        huge = contract.decimal("9" * 5000)
        x = Value("input", t, "x", pure=True, total=True)
        root = binary("plus", binary("plus", x, literal(huge, t)), literal(huge, t))
        with Driver(program) as driver:
            result = driver.optimize(root)
            self.assertEqual(result.type, t)
            self.assertEqual(result.children[1].value, huge * 2)
            self.assertEqual([e.rule for e in driver.trace], ["ReassociateAddConstants", "ConstantFolding"])

    def test_native_resolver_rejects_unchecked_ir(self):
        lib, error, lowering = library(), Error(), Lowering()
        registry = lib.bs_registry_create(C.byref(error))
        self.assertTrue(registry)
        try:
            kernel = contract.load(contract.FIXTURE)
            source = lowering.kernel(kernel)
            # The C compiler independently checks capture numbering and imports.
            source.rules[0].match[0].capture = 0
            profile = C.c_void_p()
            status = lib.bs_profile_compile(registry, C.byref(source), 1, source.name, C.byref(profile), C.byref(error))
            self.assertEqual(status, 1)
            self.assertFalse(profile.value)
            self.assertIn(b"expected $1", error.message)
            self.assertIn(b"AlgebraicSimplification.bst:29:", error.message)
            source.rules[0].match[0].capture = 1
            source.rules[3].match[1].same = 2
            self.assertEqual(lib.bs_profile_compile(registry, C.byref(source), 1, source.name, C.byref(profile), C.byref(error)), 1)
            self.assertIn(b"earlier", error.message)
        finally:
            lib.bs_registry_destroy(registry)

    def test_cyclic_native_input_and_callback_errors(self):
        program = resolve(contract.load(contract.FIXTURE))
        t = Type(width=8)
        x = Value("input", t, "x", pure=True, total=True)
        root = binary("plus", x, literal(0, t))
        object.__setattr__(root, "children", (root, x))
        with Driver(program) as driver:
            with self.assertRaisesRegex(Diagnostic, "cyclic"):
                driver.optimize(root)

        class Broken(contract.TreeAdapter):
            def literal(self, *args):
                raise Diagnostic("host allocation failed")
        with Driver(program, Broken()) as driver:
            with self.assertRaisesRegex(Diagnostic, "host allocation failed"):
                driver.optimize(binary("minus", x, x))
            self.assertEqual(driver.trace, [])

    def test_profile_lifetime_and_independent_runs(self):
        program = resolve(contract.load(contract.FIXTURE))
        with Driver(program) as driver:
            lib = driver.lib
            self.assertEqual(lib.bs_profile_rule_count(driver.profile), 17)
            self.assertEqual(lib.bs_profile_rule_name(driver.profile, 0), b"ConstantFolding")
            t = Type(width=8)
            x = Value("input", t, "x")
            node = binary("plus", x, literal(0, t))
            self.assertIs(driver.optimize(node), x)
            self.assertIs(driver.optimize(x), x)
            self.assertEqual(driver.trace, [])
        with self.assertRaisesRegex(Diagnostic, "closed"):
            driver.optimize(node)

    def test_expanding_kernel_is_bounded(self):
        kernel = parse("""Kernel Expand;
        concerns HIR::Literals::Term; concerns HIR::Expressions::Binary;
        requires Arithmetic::Domains::ModularInteger;
        Expand :: Binary(Term!resolves, Term!resolves) -> Binary where ModularInteger
        { $1 `plus` $2 ~> ($1 `plus` $2) `plus` 0; }
        """)
        t = Type(width=8)
        x, y = Value("input", t, "x"), Value("input", t, "y")
        root = binary("plus", x, y)
        with Driver(resolve(kernel), budget=4) as driver:
            with self.assertRaisesRegex(contract.Nonconvergence, "budget 4 exhausted"):
                driver.optimize(root)
            self.assertEqual(len(driver.trace), 4)
            self.assertIs(root.children[0], x)


if __name__ == "__main__":
    unittest.main()
