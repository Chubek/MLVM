"""Static declaration, capture, structure, and capability validation."""
from dataclasses import dataclass
from .frontend import Diagnostic


def decimal(spelling):
    """No machine-word or Python decimal-string conversion limit."""
    negative = spelling.startswith("-")
    digits = spelling[1:] if negative else spelling
    result = 0
    for i in range(0, len(digits), 9):
        chunk = digits[i:i + 9]
        result = result * (10 ** len(chunk)) + int(chunk)
    return -result if negative else result


def integer_spelling(value):
    """Print arbitrary-size integers without Python's decimal-string limit."""
    negative = value < 0
    value = abs(value)
    chunks = []
    while value >= 10 ** 9:
        value, chunk = divmod(value, 10 ** 9)
        chunks.append(f"{chunk:09d}")
    return ("-" if negative else "") + str(value) + "".join(reversed(chunks))


@dataclass(frozen=True)
class Operator:
    name: str
    associative: bool = False
    commutative: bool = False
    domain: str = "integer"


class Registry:
    def __init__(self):
        self.operators = {name: Operator(name, name in ("plus", "times"), name in ("plus", "times"))
                          for name in ("plus", "minus", "times", "divide", "bit_and", "bit_or", "bit_xor")}
        self.views = {"HIR::Literals::Term": "Term", "HIR::Expressions::Binary": "Binary"}
        self.refinements = {"resolves": None, "literal": None, "nonliteral": None, "exact": "integer", "same": "capture"}
        self.capabilities = {
            "Relational::Properties::Associativity": ("Associative", 0),
            "Relational::Properties::Commutativity": ("Commutative", 0),
            "Arithmetic::Domains::ModularInteger": ("ModularInteger", 0),
            "HIR::Effects::Pure": ("Pure", 1), "HIR::Effects::Total": ("Total", 1),
        }
        from .catalog import register
        register(self)


@dataclass(frozen=True)
class Folding:
    name: str = "ConstantFolding"


@dataclass(frozen=True)
class Program:
    kernel: object
    rules: tuple
    registry: Registry
    kernels: tuple = ()


def validate(kernel, registry):
    sorts = set()
    for view in kernel.concerns:
        if view.name not in registry.views:
            view.span.fail(f"unknown HIR view {view.name}")
        sorts.add(registry.views[view.name])
    predicates = {}
    for capability in kernel.requires:
        if capability.name not in registry.capabilities:
            capability.span.fail(f"unknown capability {capability.name}")
        alias, arity = registry.capabilities[capability.name]
        for short in (capability.name.split("::")[-1], alias):
            previous = predicates.get(short)
            if previous and previous[0] != capability.name:
                capability.span.fail(f"ambiguous imported predicate {short}")
            predicates[short] = (capability.name, arity)
    for rule in kernel.rules:
        if rule.result not in ("Term", "Binary"):
            rule.span.fail(f"unknown result sort {rule.result}")
        if rule.result not in sorts:
            rule.span.fail(f"result sort {rule.result} requires a concerns declaration")
        leaves = []

        def pattern(node):
            if node.sort not in sorts:
                node.span.fail(f"sort {node.sort} requires a concerns declaration")
            if node.sort == "Binary":
                for child in node.children:
                    pattern(child)
                return
            leaves.append(node)
            if node.refinement not in registry.refinements:
                node.span.fail(f"unknown refinement {node.refinement}")
            expected = registry.refinements[node.refinement]
            actual = "capture" if node.argument.startswith("$") else "integer" if node.argument else None
            if expected != actual:
                node.span.fail(f"refinement {node.refinement} expects {expected or 'no argument'}")
            if expected == "capture":
                slot = decimal(node.argument[1:])
                if not 1 <= slot < len(leaves):
                    node.span.fail("!same must reference an earlier one-based capture")
        pattern(rule.pattern)

        def capture(spelling, span):
            if not 1 <= decimal(spelling) <= len(leaves):
                span.fail(f"capture ${spelling} out of range 1..{len(leaves)}")

        def expression(node):
            if node.kind == "capture":
                capture(node.value, node.span)
            elif node.kind == "binary":
                if node.value not in registry.operators:
                    node.span.fail(f"unknown operator {node.value}")
                for child in node.children:
                    expression(child)
        expression(rule.match)
        expression(rule.build)
        domains = set()

        def operator_domains(expr):
            if expr.kind == "binary":
                domains.add(registry.operators[expr.value].domain)
                for child in expr.children:
                    operator_domains(child)
        operator_domains(rule.match)
        operator_domains(rule.build)
        host_domains = domains - {"integer"}
        if host_domains:
            from .catalog import HOST_PROOFS
            modules = {"statement": "Statements", "memory": "Memory", "control": "ControlFlow",
                       "loop": "Loops", "polyhedral": "Polyhedra"}
            if len(domains) != 1:
                rule.span.fail("a host rule cannot mix operator carrier domains")
            domain = next(iter(host_domains))
            module = modules.get(domain)
            certified = {f"Optimization::{module}::{name}" for name in HOST_PROOFS.get(module, "").split()}
            if not any(pred.name in predicates and predicates[pred.name][0] in certified for pred in rule.guard):
                rule.span.fail(f"{domain} rule requires an explicit host transformation proof")
        slot = 0

        def shape(pattern, expr):
            nonlocal slot
            if pattern.sort == "Term":
                slot += 1
                if expr.kind != "capture" or decimal(expr.value) != slot:
                    expr.span.fail(f"signature/match tree mismatch: expected ${slot}")
            else:
                if expr.kind != "binary":
                    expr.span.fail("signature/match tree mismatch: expected Binary")
                for p, e in zip(pattern.children, expr.children):
                    shape(p, e)
        shape(rule.pattern, rule.match)
        if rule.result == "Binary" and rule.build.kind != "binary":
            rule.build.span.fail("Binary result requires a binary construction")
        for pred in rule.guard:
            if pred.name not in predicates:
                pred.span.fail(f"unknown or unimported predicate {pred.name}")
            if len(pred.arguments) != predicates[pred.name][1]:
                pred.span.fail(f"predicate {pred.name} expects {predicates[pred.name][1]} arguments")
            for argument in pred.arguments:
                capture(argument, pred.span)


def resolve(kernel, kernels=None, registry=None):
    registry = registry or Registry()
    available = dict(kernels or {})
    available[kernel.name] = kernel
    rules, done, active, names, ordered = [], set(), [], set(), []

    def visit(name, span):
        if name in active:
            span.fail("inheritance cycle: " + " -> ".join(active + [name]))
        if name in done:
            return
        if name == "ConstantFolding" and name not in available:
            local = (Folding(),)
        else:
            if name not in available:
                span.fail(f"unknown inherited kernel {name}")
            current = available[name]
            active.append(name)
            for base in current.inherits:
                visit(base.name, base.span)
            active.pop()
            validate(current, registry)
            local = current.rules
            ordered.append(current)
        for rule in local:
            if rule.name in names:
                getattr(rule, "span", span).fail(f"duplicate rule name {rule.name} in inheritance graph")
            names.add(rule.name)
            rules.append(rule)
        done.add(name)
    visit(kernel.name, kernel.span)
    return Program(kernel, tuple(rules), registry, tuple(ordered))
