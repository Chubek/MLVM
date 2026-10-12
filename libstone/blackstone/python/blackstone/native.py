"""ctypes lowering to the native profile compiler and occurrence-tree driver.

The C runtime owns matching, guard scheduling and rewriting. This bridge supplies
the existing arbitrary-width HIR adapter's inspection/construction contracts.
"""
import ctypes as C
from dataclasses import replace
import os
from pathlib import Path

from .frontend import Diagnostic
from .hir import TreeAdapter, calculate
from .resolve import decimal, integer_spelling
from .rewrite import Trace, Nonconvergence, describe, same_tree

Size, U32, Value = C.c_size_t, C.c_uint32, C.c_size_t


class Span(C.Structure):
    _fields_ = [("path", C.c_char_p), ("line", U32), ("column", U32)]


class Error(C.Structure):
    _fields_ = [("code", C.c_int), ("message", C.c_char * 1024)]


class Declaration(C.Structure):
    _fields_ = [("name", C.c_char_p), ("span", Span)]


class MatchNode(C.Structure):
    _fields_ = [("sort", C.c_int), ("refinement", C.c_int), ("capture", U32), ("same", U32),
                ("left", U32), ("right", U32), ("text", C.c_char_p), ("span", Span)]


class BuildNode(C.Structure):
    _fields_ = [("kind", C.c_int), ("capture", U32), ("left", U32), ("right", U32),
                ("text", C.c_char_p), ("span", Span)]


class Guard(C.Structure):
    _fields_ = [("name", C.c_char_p), ("arguments", C.POINTER(U32)), ("count", Size), ("span", Span)]


class Rule(C.Structure):
    _fields_ = [("name", C.c_char_p), ("result", C.c_int), ("match", C.POINTER(MatchNode)),
                ("match_count", Size), ("build", C.POINTER(BuildNode)), ("build_count", Size),
                ("guards", C.POINTER(Guard)), ("guard_count", Size), ("span", Span)]


class Kernel(C.Structure):
    _fields_ = [("name", C.c_char_p), ("inherits", C.POINTER(Declaration)), ("inherit_count", Size),
                ("concerns", C.POINTER(Declaration)), ("concern_count", Size),
                ("requires", C.POINTER(Declaration)), ("require_count", Size),
                ("rules", C.POINTER(Rule)), ("rule_count", Size), ("span", Span)]


class Concern(C.Structure):
    _fields_ = [("name", C.c_char_p), ("sort", C.c_int)]


class Operator(C.Structure):
    _fields_ = [("name", C.c_char_p), ("domain", C.c_char_p), ("associative", C.c_int), ("commutative", C.c_int)]


class ConcernModule(C.Structure):
    _fields_ = [("name", C.c_char_p), ("views", C.POINTER(Concern)), ("view_count", Size),
                ("operators", C.POINTER(Operator)), ("operator_count", Size)]


class Capability(C.Structure):
    _fields_ = [("name", C.c_char_p), ("alias", C.c_char_p), ("arity", U32), ("proof_domain", C.c_char_p)]


class RequiresModule(C.Structure):
    _fields_ = [("name", C.c_char_p), ("capabilities", C.POINTER(Capability)), ("count", Size)]


class View(C.Structure):
    _fields_ = [("kind", C.c_int), ("resolved", C.c_int), ("domain", C.c_char_p), ("width", U32),
                ("is_signed", C.c_int), ("type_key", Size), ("op", C.c_char_p), ("integer", C.c_char_p),
                ("flag_count", Size), ("left", Value), ("right", Value)]


class NativeTrace(C.Structure):
    _fields_ = [("rule", C.c_char_p), ("rule_source", Span), ("before", Value), ("after", Value), ("step", Size)]


Inspect = C.CFUNCTYPE(C.c_int, C.c_void_p, Value, C.POINTER(View))
Compare = C.CFUNCTYPE(C.c_int, C.c_void_p, Value, Value)
Query = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, Value, C.POINTER(Value), Size, C.POINTER(Value), Size)
Literal = C.CFUNCTYPE(Value, C.c_void_p, C.c_char_p, Value, C.c_char_p)
Binary = C.CFUNCTYPE(Value, C.c_void_p, C.c_char_p, Value, Value, Value, C.c_char_p)
Rebuild = C.CFUNCTYPE(Value, C.c_void_p, Value, Value, Value)
Fold = C.CFUNCTYPE(Value, C.c_void_p, Value, C.c_char_p)
OnTrace = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(NativeTrace))


class Adapter(C.Structure):
    _fields_ = [("abi_version", U32), ("context", C.c_void_p), ("inspect", Inspect), ("identity", Compare),
                ("equivalent", Compare), ("query", Query), ("literal", Literal), ("binary", Binary),
                ("rebuild", Rebuild), ("fold", Fold), ("trace", OnTrace)]


class Options(C.Structure):
    _fields_ = [("budget", Size), ("max_depth", Size)]


class Stats(C.Structure):
    _fields_ = [("rewrites", Size), ("visited", Size)]


def library():
    path = os.environ.get("BLACKSTONE_LIBRARY")
    if not path:
        root = Path(__file__).resolve().parents[2] / "build"
        path = next((str(root / name) for name in ("libblackstone.so", "libblackstone.dylib", "blackstone.dll")
                     if (root / name).is_file()), None)
    if not path:
        raise Diagnostic("native runtime not found; build Blackstone and set BLACKSTONE_LIBRARY")
    try:
        lib = C.CDLL(path)
    except OSError as error:
        raise Diagnostic(f"cannot load native runtime: {error}") from error
    signatures = {
        "bs_abi_version": (U32, []),
        "bs_registry_create": (C.c_void_p, [C.POINTER(Error)]),
        "bs_registry_destroy": (None, [C.c_void_p]),
        "bs_concern_find": (C.c_void_p, [C.c_void_p, C.c_char_p]),
        "bs_operator_find": (C.c_void_p, [C.c_void_p, C.c_char_p]),
        "bs_requires_find": (C.c_void_p, [C.c_void_p, C.c_char_p]),
        "bs_concern_load": (C.c_int, [C.c_void_p, C.POINTER(ConcernModule), C.POINTER(Error)]),
        "bs_requires_load": (C.c_int, [C.c_void_p, C.POINTER(RequiresModule), C.POINTER(Error)]),
        "bs_profile_compile": (C.c_int, [C.c_void_p, C.POINTER(Kernel), Size, C.c_char_p, C.POINTER(C.c_void_p), C.POINTER(Error)]),
        "bs_profile_destroy": (None, [C.c_void_p]),
        "bs_profile_rule_count": (Size, [C.c_void_p]),
        "bs_profile_rule_name": (C.c_char_p, [C.c_void_p, Size]),
        "bs_optimize": (C.c_int, [C.c_void_p, C.POINTER(Adapter), Value, C.POINTER(Options), C.POINTER(Value), C.POINTER(Stats), C.POINTER(Error)]),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(lib, name)
        function.restype, function.argtypes = result, arguments
    if lib.bs_abi_version() != 1:
        raise Diagnostic("unsupported Blackstone runtime ABI")
    return lib


class Lowering:
    def __init__(self):
        self.keep = []

    def string(self, value):
        data = str(value).encode("utf-8")
        if b"\0" in data:
            raise Diagnostic("native IR strings cannot contain NUL")
        self.keep.append(data)
        return data

    def array(self, kind, values):
        result = (kind * len(values))(*values)
        self.keep.append(result)
        return result

    def span(self, value):
        return Span(self.string(value.path), value.line, value.column)

    def capture(self, spelling):
        number = decimal(spelling)
        if not 1 <= number <= 0xffffffff:
            raise Diagnostic("native capture index must be in 1..4294967295")
        return number

    def rule(self, rule):
        matches, builds = [], []

        def match(pattern, expr):
            if pattern.sort == "Binary":
                if expr.kind != "binary":
                    expr.span.fail("signature/match tree mismatch: expected Binary")
                left, right = [match(p, e) for p, e in zip(pattern.children, expr.children)]
                node = MatchNode(2, 0, 0, 0, left, right, self.string(expr.value), self.span(pattern.span))
            else:
                if expr.kind != "capture":
                    expr.span.fail("signature/match tree mismatch: expected capture")
                refinement = {"resolves": 1, "literal": 2, "nonliteral": 3, "exact": 4, "same": 5}[pattern.refinement]
                same = self.capture(pattern.argument[1:]) if refinement == 5 else 0
                node = MatchNode(1, refinement, self.capture(expr.value), same, 0, 0,
                                 self.string(pattern.argument if refinement == 4 else ""), self.span(pattern.span))
            matches.append(node)
            return len(matches) - 1

        def build(expr):
            if expr.kind == "binary":
                left, right = [build(c) for c in expr.children]
                node = BuildNode(3, 0, left, right, self.string(expr.value), self.span(expr.span))
            else:
                node = BuildNode(1 if expr.kind == "capture" else 2,
                                 self.capture(expr.value) if expr.kind == "capture" else 0, 0, 0,
                                 self.string(expr.value if expr.kind == "integer" else ""), self.span(expr.span))
            builds.append(node)
            return len(builds) - 1
        match(rule.pattern, rule.match)
        build(rule.build)
        guards = [Guard(self.string(p.name), self.array(U32, [self.capture(a) for a in p.arguments]),
                        len(p.arguments), self.span(p.span)) for p in rule.guard]
        return Rule(self.string(rule.name), 1 if rule.result == "Term" else 2,
                    self.array(MatchNode, matches), len(matches), self.array(BuildNode, builds), len(builds),
                    self.array(Guard, guards), len(guards), self.span(rule.span))

    def kernel(self, kernel):
        def declarations(items):
            return self.array(Declaration, [Declaration(self.string(i.name), self.span(i.span)) for i in items])
        return Kernel(self.string(kernel.name), declarations(kernel.inherits), len(kernel.inherits),
                      declarations(kernel.concerns), len(kernel.concerns), declarations(kernel.requires), len(kernel.requires),
                      self.array(Rule, [self.rule(r) for r in kernel.rules]), len(kernel.rules), self.span(kernel.span))

    def register(self, lib, registry, source, error):
        from .catalog import HOST_PROOFS
        modules = {"Statements": "statement", "Memory": "memory", "ControlFlow": "control",
                   "Loops": "loop", "Polyhedra": "polyhedral"}
        proofs = {f"Optimization::{m}::{name}": modules[m] for m, names in HOST_PROOFS.items() for name in names.split()}
        views = [Concern(self.string(n), 1 if sort == "Term" else 2) for n, sort in source.views.items()
                 if not lib.bs_concern_find(registry, n.encode())]
        operators = [Operator(self.string(n), self.string(op.domain), op.associative, op.commutative)
                     for n, op in source.operators.items() if not lib.bs_operator_find(registry, n.encode())]
        concern = ConcernModule(b"Frontend", self.array(Concern, views), len(views), self.array(Operator, operators), len(operators))
        capabilities = [Capability(self.string(n), self.string(alias), arity, self.string(proofs.get(n, "")))
                        for n, (alias, arity) in source.capabilities.items() if not lib.bs_requires_find(registry, n.encode())]
        requires = RequiresModule(b"Frontend", self.array(Capability, capabilities), len(capabilities))
        if lib.bs_concern_load(registry, C.byref(concern), C.byref(error)) or lib.bs_requires_load(registry, C.byref(requires), C.byref(error)):
            raise Diagnostic(error.message.decode())


class Driver:
    """Same public optimize/trace boundary as rewrite.Driver; supports custom adapters."""
    def __init__(self, program, adapter=None, budget=10000):
        if type(budget) is not int or budget < 0 or budget > Size(-1).value:
            raise Diagnostic("rewrite budget must be a native-sized nonnegative integer")
        self.program, self.adapter, self.budget = program, adapter or TreeAdapter(), budget
        self.trace, self.stats = [], None
        self.lib, self.registry, self.profile = library(), None, C.c_void_p()
        error, lowering = Error(), Lowering()
        try:
            self.registry = self.lib.bs_registry_create(C.byref(error))
            if not self.registry:
                raise Diagnostic(error.message.decode())
            lowering.register(self.lib, self.registry, program.registry, error)
            kernels = lowering.array(Kernel, [lowering.kernel(k) for k in program.kernels or (program.kernel,)])
            if self.lib.bs_profile_compile(self.registry, kernels, len(kernels), program.kernel.name.encode(),
                                           C.byref(self.profile), C.byref(error)):
                raise Diagnostic(error.message.decode())
        except BaseException:
            self.close()
            raise

    def close(self):
        profile = getattr(self, "profile", None)
        if profile is not None and profile.value:
            self.lib.bs_profile_destroy(profile)
            self.profile = C.c_void_p()
        if getattr(self, "registry", None):
            self.lib.bs_registry_destroy(self.registry)
            self.registry = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        self.close()

    def optimize(self, root):
        if not self.profile.value:
            raise Diagnostic("native driver is closed")
        self.trace = []
        values, strings, failures = {}, [], []
        adapter, registry = self.adapter, self.program.registry

        def hold(value):
            if value is None:
                return 0
            values[id(value)] = value
            return id(value)

        def string(value):
            result = value.encode()
            strings.append(result)
            return result

        def protect(fallback, fn):
            def call(*args):
                if failures:
                    return fallback
                try:
                    return fn(*args)
                except BaseException as error:
                    failures.append(error)
                    return fallback
            return call

        def inspect(ctx, handle, pointer):
            v = values[handle]
            if not 1 <= v.type.width <= 0xffffffff:
                raise Diagnostic("native HIR width must be in 1..4294967295")
            if v.kind == "binary" and len(v.children) != 2:
                raise Diagnostic("binary HIR node requires two operands")
            pointer[0] = View({"input": 1, "literal": 2, "binary": 3}[v.kind], v.resolved, string(v.type.domain),
                              v.type.width, v.type.signed, 0, string(v.name),
                              string(integer_spelling(v.value)) if v.kind == "literal" else None, len(v.flags),
                              hold(v.children[0]) if v.kind == "binary" else 0, hold(v.children[1]) if v.kind == "binary" else 0)
            return 1

        def query(ctx, name, root, args, argc, skeleton, count):
            fact = adapter.predicate(name.decode(), values[root], [values[args[i]] for i in range(argc)],
                                     [values[skeleton[i]] for i in range(count)], registry)
            if fact is not None and type(fact) is not bool:
                raise Diagnostic("adapter predicate must return true, false, or unknown")
            return -1 if fact is None else int(fact)

        def literal(ctx, spelling, root, rule):
            source, number = values[root], decimal(spelling.decode())
            return hold(adapter.literal(number, source.type, source, rule.decode())) if source.type.represents(number) else 0

        def binary(ctx, name, left, right, root, rule):
            source = values[root]
            return hold(adapter.binary(name.decode(), (values[left], values[right]), source.type, source, rule.decode()))

        def fold(ctx, root, rule):
            source = values[root]
            number = calculate(source.name, source.children[0].value, source.children[1].value, source.type)
            return hold(adapter.literal(number, source.type, source, rule.decode())) if number is not None else 0

        def trace(ctx, pointer):
            event = pointer.contents
            span = event.rule_source
            location = span.path.decode()
            if location != "<builtin>":
                location = f"{location}:{span.line}:{span.column}"
            self.trace.append(Trace(event.rule.decode(), values[event.before].provenance, location,
                                    describe(values[event.before]), describe(values[event.after])))

        callbacks = Adapter(1, None, Inspect(protect(0, inspect)),
                            Compare(protect(0, lambda ctx, a, b: int(adapter.identity(values[a], values[b])))),
                            Compare(protect(0, lambda ctx, a, b: int(same_tree(values[a], values[b])))),
                            Query(protect(-1, query)), Literal(protect(0, literal)), Binary(protect(0, binary)),
                            Rebuild(protect(0, lambda ctx, root, a, b: hold(replace(values[root], children=(values[a], values[b]))))),
                            Fold(protect(0, fold)), OnTrace(protect(None, trace)))
        result, stats, error = Value(), Stats(), Error()
        options = Options(self.budget, 256)
        status = self.lib.bs_optimize(self.profile, C.byref(callbacks), hold(root), C.byref(options),
                                      C.byref(result), C.byref(stats), C.byref(error))
        self.stats = stats
        if failures:
            raise failures[0]
        if status == 3:
            failure = Nonconvergence(self.budget, self.trace, "native rewrite")
            failure.args = (error.message.decode(),)
            raise failure
        if status:
            raise Diagnostic(error.message.decode())
        return values[result.value]
