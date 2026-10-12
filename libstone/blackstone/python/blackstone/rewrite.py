"""Deterministic postorder, base-first guarded rewriting to a fixed point."""
from dataclasses import dataclass, replace
from .frontend import Diagnostic
from .resolve import Folding, decimal, integer_spelling
from .hir import TreeAdapter, modular, calculate


def same_tree(left, right):
    if left is right:
        return True
    if left.kind != right.kind or left.type != right.type:
        return False
    if left.kind == "literal":
        return left.value == right.value
    if left.kind != "binary":
        return False
    return (left.name == right.name and left.flags == right.flags and
            left.resolved == right.resolved and left.pure == right.pure and left.total == right.total and
            all(same_tree(a, b) for a, b in zip(left.children, right.children)))


def describe(node):
    if node.kind == "literal":
        return integer_spelling(node.value)
    if node.kind == "input":
        return node.name
    return f"({describe(node.children[0])} {node.name} {describe(node.children[1])})"


@dataclass(frozen=True)
class Trace:
    rule: str
    source: str
    rule_source: str
    before: str
    after: str


class Nonconvergence(Diagnostic):
    def __init__(self, budget, trace, pending):
        self.trace = tuple(trace)
        super().__init__(f"rewrite budget {budget} exhausted before {pending}; last rules: " +
                         ", ".join(event.rule for event in trace[-8:]))


def match(rule, root, adapter):
    captures, skeleton = [], []

    def visit(pattern, expr, value):
        if not value.resolved:
            return False
        if pattern.sort == "Binary":
            if value.kind != "binary" or value.name != expr.value:
                return False
            skeleton.append(value)
            return all(visit(p, e, v) for p, e, v in zip(pattern.children, expr.children, value.children))
        captures.append(value)
        refine = pattern.refinement
        if refine == "resolves":
            return True
        if refine == "literal":
            return value.kind == "literal"
        if refine == "nonliteral":
            return value.kind != "literal"
        if refine == "exact":
            integer = decimal(pattern.argument)
            return value.kind == "literal" and value.type.represents(integer) and value.value == integer
        if refine == "same":
            return adapter.identity(value, captures[decimal(pattern.argument[1:]) - 1])
        return False
    return (captures, skeleton) if visit(rule.pattern, rule.match, root) else None


class Driver:
    def __init__(self, program, adapter=None, budget=10000):
        if type(budget) is not int or budget < 0:
            raise Diagnostic("rewrite budget must be a nonnegative integer")
        self.program = program
        self.adapter = adapter or TreeAdapter()
        self.budget = budget
        self.trace = []

    def candidate(self, rule, root):
        registry, adapter = self.program.registry, self.adapter
        if isinstance(rule, Folding):
            if not modular([root], registry) or not all(c.kind == "literal" for c in root.children):
                return None
            folded = calculate(root.name, root.children[0].value, root.children[1].value, root.type)
            return None if folded is None else adapter.literal(folded, root.type, root, rule.name)
        result = match(rule, root, adapter)
        if result is None:
            return None
        captures, skeleton = result
        for node in skeleton:
            if registry.operators[node.name].domain != "integer" and not adapter.can_construct(skeleton, root, registry, node.name):
                return None
        for pred in rule.guard:
            args = [captures[decimal(arg) - 1] for arg in pred.arguments]
            if adapter.predicate(pred.name, root, args, skeleton, registry) is not True:
                return None

        def build(expr):
            if expr.kind == "capture":
                value = captures[decimal(expr.value) - 1]
                return value if value.type == root.type else None
            if expr.kind == "integer":
                number = decimal(expr.value)
                return adapter.literal(number, root.type, root, rule.name) if root.type.represents(number) else None
            if not adapter.can_construct(skeleton, root, registry, expr.value):
                return None
            children = tuple(build(c) for c in expr.children)
            if any(c is None for c in children):
                return None
            return adapter.binary(expr.value, children, root.type, root, rule.name)
        replacement = build(rule.build)
        if replacement is not None and (replacement.type != root.type or not replacement.resolved or
                                        (rule.result == "Binary" and replacement.kind != "binary")):
            raise Diagnostic(f"adapter violated replacement contract for {rule.name}")
        return replacement

    def optimize(self, root):
        self.trace = []
        active, checked = set(), set()

        def acyclic(node):
            if id(node) in active:
                raise Diagnostic("cyclic expression input")
            if id(node) in checked:
                return
            if node.kind == "binary" and len(node.children) != 2:
                raise Diagnostic("binary HIR node requires two operands")
            active.add(id(node))
            for child in node.children:
                acyclic(child)
            active.remove(id(node))
            checked.add(id(node))
        acyclic(root)
        memo = {}

        def visit(node):
            if node in memo:
                return memo[node]
            original = node
            while True:
                children = tuple(visit(c) for c in node.children)
                if any(a is not b for a, b in zip(children, node.children)):
                    # Preserve host metadata for an existing operator with updated operands.
                    node = replace(node, children=children)
                replacement = None
                for rule in self.program.rules:
                    candidate = self.candidate(rule, node)
                    if candidate is None or same_tree(node, candidate):
                        continue
                    if len(self.trace) >= self.budget:
                        raise Nonconvergence(self.budget, self.trace, rule.name)
                    source = getattr(rule, "span", None)
                    self.trace.append(Trace(rule.name, node.provenance,
                                            f"{source.path}:{source.line}:{source.column}" if source else "<builtin>",
                                            describe(node), describe(candidate)))
                    replacement = candidate
                    break
                if replacement is None:
                    memo[original] = node
                    return node
                node = replacement
        return visit(root)
