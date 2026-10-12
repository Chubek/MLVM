"""Examples use the established language and resolve against the stdlib."""

import unittest
from pathlib import Path

from optilisp.errors import DiagnosticSink
from optilisp.semantic import analyze_file
from optilisp.sexpr import read
from optilisp.validate import (
    load_registry, validate_all, validate_kernel_against_schema,
    validate_references, validate_semantic, validate_structure,
    validate_transformation,
)

ROOT = Path(__file__).resolve().parents[2]


class TestExamples(unittest.TestCase):
    def test_examples(self):
        sink = DiagnosticSink()
        registry = load_registry(ROOT / "optilisp" / "stdlib", sink)
        for path in sorted((ROOT / "optilisp" / "examples").glob("*.olisp")):
            forms, diagnostics = read(path.read_text(encoding="utf-8"), str(path))
            sink.items.extend(diagnostics.items)
            records = analyze_file(forms, str(path), sink)
            self.assertTrue(any(records.values()))
            for kernel in records["kernels"]:
                validate_structure(kernel, forms[0], sink)
                validate_references(kernel, registry, {kernel.id}, sink)
                validate_semantic(kernel, registry, sink)
                validate_transformation(kernel, sink)
                validate_kernel_against_schema(kernel, sink)
        self.assertFalse(sink.has_errors(), sink.render())

    def test_production_database(self):
        sink = DiagnosticSink()
        database = validate_all(ROOT, sink=sink)
        self.assertTrue(database.kernels)
        self.assertFalse(sink.has_errors(), sink.render())
