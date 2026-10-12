"""Value-owned AST converted from the native dparser concrete syntax tree."""
from dataclasses import dataclass, asdict
from pathlib import Path
import json
import os
import re
import subprocess


class Diagnostic(ValueError):
    pass


@dataclass(frozen=True)
class Span:
    path: str
    start: int
    end: int
    line: int
    column: int

    def fail(self, message):
        raise Diagnostic(f"{self.path}:{self.line}:{self.column}: {message}")


@dataclass(frozen=True)
class Expr:
    kind: str
    value: str
    children: tuple
    span: Span


@dataclass(frozen=True)
class Pattern:
    sort: str
    refinement: str
    argument: str
    children: tuple
    span: Span


@dataclass(frozen=True)
class Predicate:
    name: str
    arguments: tuple
    span: Span


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: Pattern
    result: str
    guard: tuple
    match: Expr
    build: Expr
    span: Span


@dataclass(frozen=True)
class Declaration:
    name: str
    span: Span


@dataclass(frozen=True)
class Kernel:
    name: str
    inherits: tuple
    concerns: tuple
    requires: tuple
    rules: tuple
    span: Span


def ast_json(kernel):
    return json.dumps(asdict(kernel), indent=2)


def parser_path():
    path = os.environ.get("BLACKSTONE_PARSER")
    if path:
        return path
    root = Path(__file__).resolve().parents[2]
    path = root / "build" / "blackstone-parser"
    if path.is_file():
        return str(path)
    raise Diagnostic("native parser not found; build Blackstone and set BLACKSTONE_PARSER")


def parse(source, path="<input>"):
    # ASCII token syntax; offsets are bytes, not Unicode character indices.
    raw = source.encode("utf-8")
    if b"\0" in raw:
        raise Diagnostic(f"{path}: embedded NUL is not permitted")
    try:
        process = subprocess.run([parser_path()], input=raw, capture_output=True)
    except OSError as error:
        raise Diagnostic(f"cannot run native parser: {error}") from error
    if process.returncode:
        message = process.stderr.decode(errors="replace").strip()
        location = re.search(r"line (\d+), column (\d+)", message)
        prefix = f"{path}:{location[1]}:{location[2]}" if location else path
        raise Diagnostic(f"{prefix}: {message or 'native parser failed'}")
    tree = json.loads(process.stdout)

    def children(node):
        result = []
        for child in node["children"]:
            if "__" in child["kind"]:
                result.extend(children(child))
            else:
                result.append(child)
        return result

    def named(node, kind):
        return [child for child in children(node) if child["kind"] == kind]

    def text(node):
        return raw[node["start"]:node["end"]].decode("utf-8")

    def span(node):
        before = raw[:node["start"]]
        return Span(str(path), node["start"], node["end"], before.count(b"\n") + 1,
                    len(before.rsplit(b"\n", 1)[-1]) + 1)

    def identifier(node):
        return text(named(node, "identifier")[0])

    def capture(node):
        return text(named(node, "digits")[0])

    def integer(node):
        return ("-" if any(c["kind"] == "-" for c in children(node)) else "") + text(named(node, "digits")[0])

    def expression(node):
        kind = node["kind"]
        if kind == "capture":
            return Expr("capture", capture(node), (), span(node))
        if kind == "integer":
            return Expr("integer", integer(node), (), span(node))
        if kind in ("match_atom", "build_atom"):
            return expression(next(c for c in children(node) if c["kind"] in ("capture", "integer", "match", "build")))
        parts = [c for c in children(node) if c["kind"] in ("match_atom", "build_atom", "operator")]
        result = expression(parts[0])
        for i in range(1, len(parts), 2):
            result = Expr("binary", identifier(parts[i]), (result, expression(parts[i + 1])), span(node))
        return result

    def pattern(node):
        nested = named(node, "pattern")
        if nested:
            return Pattern("Binary", "", "", tuple(pattern(c) for c in nested), span(node))
        refine = named(node, "refinement")[0]
        args = [c for c in children(refine) if c["kind"] in ("integer", "capture")]
        argument = ""
        if args:
            argument = integer(args[0]) if args[0]["kind"] == "integer" else "$" + capture(args[0])
        return Pattern("Term", identifier(refine), argument, (), span(node))

    def guard(node):
        result = []
        for atom in named(node, "guard_atom"):
            nested = named(atom, "guard")
            if nested:
                result.extend(guard(nested[0]))
            else:
                args = named(atom, "arguments")
                result.append(Predicate(identifier(atom), tuple(capture(c) for c in named(args[0], "capture")) if args else (), span(atom)))
        return tuple(result)

    def qualified(node):
        return "::".join(text(c) for c in named(node, "identifier"))

    def declaration(node):
        return Declaration(qualified(node), span(node))

    header = named(tree, "kernel")[0]
    inheritance = named(header, "inheritance")
    bases = tuple(declaration(c) for c in named(inheritance[0], "qualified")) if inheritance else ()
    concerns, requires = [], []
    for decl in named(tree, "declaration"):
        child = children(decl)[0]
        name = qualified(named(child, "qualified")[0])
        groups = named(child, "group")
        names = [name + "::" + text(c) for c in named(groups[0], "identifier")] if groups else [name]
        target = concerns if child["kind"] == "concern" else requires
        target.extend(Declaration(name, span(child)) for name in names)
    rules = []
    for node in named(tree, "rule"):
        ids = named(node, "identifier")
        rules.append(Rule(text(ids[0]), pattern(named(node, "pattern")[0]), text(ids[1]),
                          guard(named(node, "guard")[0]), expression(named(node, "match")[0]),
                          expression(named(node, "build")[0]), span(node)))
    return Kernel(identifier(header), bases, tuple(concerns), tuple(requires), tuple(rules), span(tree))


def load(path):
    return parse(Path(path).read_text(), str(path))
