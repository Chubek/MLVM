"""Immutable occurrence-tree adapter. Shared nodes mean identity, not memoized evaluation."""
from dataclasses import dataclass, replace
from .frontend import Diagnostic
from .resolve import decimal, integer_spelling


@dataclass(frozen=True)
class Type:
    domain: str = "integer"
    width: int = 32
    signed: bool = False

    @property
    def integer(self):
        return self.domain == "integer" and self.width >= 1

    def represents(self, value):
        if not self.integer:
            return False
        if self.signed:
            return -(1 << (self.width - 1)) <= value < (1 << (self.width - 1))
        return 0 <= value < (1 << self.width)

    def normalize(self, value):
        value %= 1 << self.width
        if self.signed and value >= 1 << (self.width - 1):
            value -= 1 << self.width
        return value


@dataclass(frozen=True, eq=False)
class Value:
    kind: str
    type: Type
    name: str = ""
    value: object = None
    children: tuple = ()
    flags: tuple = ()
    pure: object = None
    total: object = None
    resolved: bool = True
    provenance: str = ""
    origin: str = ""


def literal(value, type=Type(), provenance="", origin=""):
    if not type.represents(value):
        raise Diagnostic(f"literal {integer_spelling(value)} is not representable in {type}")
    return Value("literal", type, value=value, pure=True, total=True,
                 provenance=provenance, origin=origin)


def binary(op, left, right, type=None, flags=(), provenance="", origin=""):
    return Value("binary", type or left.type, name=op, children=(left, right), flags=tuple(flags),
                 provenance=provenance, origin=origin)


def modular(nodes, registry):
    if not nodes:
        return False
    type = nodes[0].type
    return type.integer and all(n.resolved and n.type == type and not n.flags and
                               n.name in registry.operators and registry.operators[n.name].domain == "integer" and
                               all(c.resolved and c.type == type for c in n.children) for n in nodes)


def calculate(op, left, right, type):
    if op == "plus": value = left + right
    elif op == "minus": value = left - right
    elif op == "times": value = left * right
    elif op == "bit_and": value = left & right
    elif op == "bit_or": value = left | right
    elif op == "bit_xor": value = left ^ right
    elif op == "divide":
        if right == 0 or (type.signed and left == -(1 << (type.width - 1)) and right == -1):
            return None
        value = abs(left) // abs(right)
        if (left < 0) != (right < 0): value = -value
    else:
        return None
    return type.normalize(value)


class TreeAdapter:
    """Compiler integrations replace these queries/construction operations."""
    def identity(self, left, right):
        return left is right and left.type == right.type

    def fact(self, name, value, registry):
        explicit = value.pure if name == "Pure" else value.total
        if explicit is not None:
            return explicit
        if value.kind != "binary":
            return None
        facts = [self.fact(name, child, registry) for child in value.children]
        if False in facts:
            return False
        if not all(fact is True for fact in facts) or not modular([value], registry):
            return None
        if name == "Pure":
            return True
        if value.name != "divide":
            return True
        left, right = value.children
        if right.kind != "literal" or right.value == 0:
            return None
        if value.type.signed and right.value == -1:
            if left.kind != "literal":
                return None
            return calculate("divide", left.value, right.value, value.type) is not None
        return True

    def predicate(self, name, root, captures, skeleton, registry):
        if name == "ModularInteger":
            return modular(skeleton, registry)
        if name in ("Associative", "Associativity", "Commutative", "Commutativity"):
            if not modular([root], registry):
                return False
            op = registry.operators[root.name]
            return op.associative if name in ("Associative", "Associativity") else op.commutative
        if name in ("Pure", "Total"):
            return self.fact(name, captures[0], registry)
        if name in ("AllOnes", "Nonzero"):
            value = captures[0]
            if value.kind != "literal" or not value.resolved or not value.type.integer:
                return None
            return value.value == value.type.normalize(-1) if name == "AllOnes" else value.value != 0
        return None

    def can_construct(self, skeleton, root, registry, operator):
        target = registry.operators[operator]
        if target.domain == "integer":
            return modular(skeleton, registry)
        # Metadata eligibility, independent of host proof queries. Construction
        # additionally requires the driver's guards. Embedded payloads are opaque.
        return (root.type.domain == target.domain and bool(skeleton) and
                all(n.resolved and n.type == root.type and not n.flags and
                    registry.operators[n.name].domain == target.domain and
                    all(c.resolved and c.type == root.type for c in n.children) for n in skeleton))

    def literal(self, value, type, source, rule):
        return literal(value, type, source.provenance, rule)

    def binary(self, op, children, type, source, rule):
        # Construction runs only after proof guards and carrier checks succeed.
        return binary(op, *children, type=type, provenance=source.provenance, origin=rule)


def load_fixture(data):
    """DAG-shaped serialization with occurrence-tree semantics; reject cycles."""
    if not isinstance(data, dict) or not isinstance(data.get("nodes"), dict):
        raise Diagnostic("HIR fixture requires an object containing nodes and root")
    specs, cache, active = data["nodes"], {}, set()

    def build(id):
        if not isinstance(id, str) or id not in specs:
            raise Diagnostic(f"unknown HIR node {id!r}")
        if id in active:
            raise Diagnostic(f"cyclic HIR input at {id}")
        if id in cache:
            return cache[id]
        active.add(id)
        spec = specs[id]
        if not isinstance(spec, dict):
            raise Diagnostic(f"HIR node {id} must be an object")
        t = spec.get("type", {})
        if not isinstance(t, dict):
            raise Diagnostic(f"HIR node {id}: type must be an object")
        width, signed = t.get("width", 32), t.get("signed", False)
        if type(width) is not int or width < 1 or type(signed) is not bool:
            raise Diagnostic(f"HIR node {id}: invalid type width/signedness")
        domain = t.get("domain", "integer")
        if not isinstance(domain, str):
            raise Diagnostic(f"HIR node {id}: domain must be a string")
        type_ = Type(domain, width, signed)
        kind = spec.get("kind")
        facts = [spec.get("pure"), spec.get("total")]
        if any(f is not None and type(f) is not bool for f in facts):
            raise Diagnostic(f"HIR node {id}: facts must be true, false, or null")
        resolved = spec.get("resolved", True)
        if type(resolved) is not bool:
            raise Diagnostic(f"HIR node {id}: resolved must be Boolean")
        source = spec.get("provenance", id)
        if any(not isinstance(spec.get(key, ""), str) for key in ("provenance", "origin", "op")):
            raise Diagnostic(f"HIR node {id}: op, provenance, and origin must be strings")
        if kind == "literal":
            spelling = str(spec.get("value", ""))
            import re
            if not re.fullmatch(r"-?[0-9]+", spelling):
                raise Diagnostic(f"HIR node {id}: invalid decimal literal")
            value = literal(decimal(spelling), type_, source)
        elif kind in ("input", "binary"):
            args = spec.get("args", [])
            if kind == "binary" and (not isinstance(args, list) or len(args) != 2):
                raise Diagnostic(f"HIR node {id}: binary requires two args")
            flags = spec.get("flags", [])
            if not isinstance(flags, list) or not all(isinstance(f, str) for f in flags):
                raise Diagnostic(f"HIR node {id}: flags must be a string list")
            value = Value(kind, type_, name=spec.get("op", id), children=tuple(build(a) for a in args) if kind == "binary" else (),
                          flags=tuple(flags), pure=facts[0], total=facts[1], resolved=resolved, provenance=source)
        else:
            raise Diagnostic(f"HIR node {id}: unknown kind {kind!r}")
        value = replace(value, origin=spec.get("origin", ""), resolved=resolved)
        active.remove(id)
        cache[id] = value
        return value
    # Validate unreachable nodes as well, so cycles never enter the adapter.
    for id in specs:
        build(id)
    return build(data.get("root"))


def dump_fixture(root):
    nodes, ids = {}, {}

    def visit(value):
        if id(value) in ids:
            return ids[id(value)]
        key = f"n{len(ids)}"
        ids[id(value)] = key
        spec = {"kind": value.kind, "type": {"domain": value.type.domain, "width": value.type.width, "signed": value.type.signed},
                "provenance": value.provenance, "origin": value.origin}
        if value.kind == "binary":
            spec.update(op=value.name, args=[visit(c) for c in value.children], flags=list(value.flags))
        elif value.kind == "literal":
            spec["value"] = integer_spelling(value.value)
        else:
            spec["op"] = value.name
        spec.update(pure=value.pure, total=value.total, resolved=value.resolved)
        nodes[key] = spec
        return key
    key = visit(root)
    return {"root": key, "nodes": nodes}
