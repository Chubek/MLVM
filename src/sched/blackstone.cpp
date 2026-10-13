#include <mlvm/sched.hpp>
#include <bit>
#include <charconv>
#include <cstring>
#include <limits>

namespace mlvm::sched {
namespace {
struct Term {
    BSValueKind kind = BS_INPUT_VALUE;
    unsigned width = 0;
    bool signed_ = true, flags = false;
    std::string domain = "integer", op, integer, provenance, origin;
    uintptr_t type = 0;
    BSFact pure = BS_UNKNOWN, total = BS_UNKNOWN;
    Term *left = nullptr, *right = nullptr;
    std::optional<std::size_t> binding;
    std::map<std::string, std::string, std::less<>> attributes;
    firestone::ir::Type fire_type;
};
Term& term(BSValue v) { return *reinterpret_cast<Term*>(v); }
BSValue handle(Term* t) { return reinterpret_cast<BSValue>(t); }
std::uint64_t mask(unsigned width) { return width == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << width) - 1; }
bool number(std::string_view s, const Term& t, std::uint64_t& bits) {
    if (t.domain != "integer" || !t.width || t.width > 64 || s.empty()) return false;
    bool negative = s.front() == '-'; if (negative) s.remove_prefix(1);
    std::uint64_t magnitude = 0; auto r = std::from_chars(s.data(), s.data() + s.size(), magnitude);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return false;
    auto limit = t.signed_ ? (std::uint64_t{1} << (t.width - 1)) : mask(t.width);
    if (negative) { if ((!t.signed_ && magnitude != 0) || magnitude > limit) return false; }
    else if (magnitude > limit - (t.signed_ ? 1 : 0)) return false;
    bits = (negative ? std::uint64_t{0} - magnitude : magnitude) & mask(t.width); return true;
}
std::string decimal(std::uint64_t bits, const Term& t) {
    bits &= mask(t.width);
    if (t.signed_ && (bits & (std::uint64_t{1} << (t.width - 1))))
        return "-" + std::to_string((~bits & mask(t.width)) + 1);
    return std::to_string(bits);
}
bool same_type(const Term& a, const Term& b) {
    return a.domain == b.domain && a.width == b.width && a.signed_ == b.signed_ && a.type == b.type;
}
bool modular(const Term& t) {
    return t.kind == BS_BINARY_VALUE && t.domain == "integer" && t.width && t.width <= 64 && !t.flags &&
        same_type(t, *t.left) && same_type(t, *t.right) &&
        (t.op == "plus" || t.op == "minus" || t.op == "times" || t.op == "divide" ||
         t.op == "bit_and" || t.op == "bit_or" || t.op == "bit_xor");
}
class Adapter {
public:
    std::deque<Term> terms;
    std::vector<Event> trace;
    bool failed = false;
    Term* add(Term t) { terms.push_back(std::move(t)); return &terms.back(); }
    Term* literal(std::string_view s, const Term& source, std::string rule) {
        std::uint64_t bits = 0;
        if (!number(s, source, bits)) return nullptr;
        Term t = source; t.kind = BS_LITERAL_VALUE; t.integer = decimal(bits, source); t.op.clear();
        t.left = t.right = nullptr; t.binding.reset(); t.flags = false;
        t.pure = t.total = BS_TRUE; t.origin = std::move(rule); return add(std::move(t));
    }
    Term* binary(std::string op, Term* a, Term* b, const Term& source, std::string rule) {
        Term t = source; t.op = std::move(op); t.left = a; t.right = b; t.binding.reset();
        t.kind = BS_BINARY_VALUE; t.integer.clear(); t.flags = false; t.origin = std::move(rule);
        if (!modular(t)) return nullptr;
        t.pure = a->pure == BS_TRUE && b->pure == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
        t.total = t.op != "divide" && a->total == BS_TRUE && b->total == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
        return add(std::move(t));
    }
    Term* fold(Term& t, const char* rule) {
        if (!modular(t) || t.left->kind != BS_LITERAL_VALUE || t.right->kind != BS_LITERAL_VALUE) return nullptr;
        std::uint64_t a, b, value = 0;
        if (!number(t.left->integer, t, a) || !number(t.right->integer, t, b)) return nullptr;
        if (t.op == "plus") value = a + b;
        else if (t.op == "minus") value = a - b;
        else if (t.op == "times") value = a * b;
        else if (t.op == "bit_and") value = a & b;
        else if (t.op == "bit_or") value = a | b;
        else if (t.op == "bit_xor") value = a ^ b;
        else if (t.op == "divide") {
            if (!b) return nullptr;
            auto sign = std::uint64_t{1} << (t.width - 1);
            if (t.signed_) {
                if (a == sign && b == mask(t.width)) return nullptr;
                bool an = a & sign, bn = b & sign;
                auto am = an ? (~a & mask(t.width)) + 1 : a;
                auto bm = bn ? (~b & mask(t.width)) + 1 : b;
                value = am / bm; if (an != bn) value = std::uint64_t{0} - value;
            } else value = a / b;
        } else return nullptr;
        return literal(decimal(value, t), t, rule);
    }
    template<class F, class T> static T protect(void* ctx, T failure, F&& fn) noexcept {
        try { return fn(*static_cast<Adapter*>(ctx)); }
        catch (...) { static_cast<Adapter*>(ctx)->failed = true; return failure; }
    }
    BSAdapter callbacks() {
        BSAdapter a{}; a.abi_version = BS_ABI_VERSION; a.context = this;
        a.inspect = [](void*, BSValue v, BSValueView* view) -> int {
            const auto& t = term(v);
            *view = {t.kind, t.width != 0, t.domain.c_str(), t.width, t.signed_, t.type,
                t.op.c_str(), t.integer.c_str(), static_cast<std::size_t>(t.flags), handle(t.left), handle(t.right)};
            return 1;
        };
        a.identity = [](void*, BSValue x, BSValue y) -> int { return x == y; };
        a.equivalent = [](void*, BSValue x, BSValue y) -> int {
            if (x == y) return 1;
            const auto& a = term(x); const auto& b = term(y);
            return same_type(a, b) && a.kind == b.kind && a.kind == BS_LITERAL_VALUE && a.integer == b.integer;
        };
        a.query = [](void*, const char* name, BSValue root, const BSValue* args, std::size_t argc, const BSValue* nodes, std::size_t count) -> BSFact {
            const auto& t = term(root); std::string_view n(name);
            if (n == "ModularInteger" || n == "Associative" || n == "Commutative") {
                if (!count) return BS_FALSE;
                for (std::size_t i = 0; i < count; ++i) if (!modular(term(nodes[i]))) return BS_FALSE;
                return n == "ModularInteger" || t.op == "plus" || t.op == "times" ? BS_TRUE : BS_FALSE;
            }
            if (argc == 1) {
                const auto& arg = term(args[0]);
                if (n == "Pure") return arg.pure;
                if (n == "Total") return arg.total;
                std::uint64_t bits;
                if ((n == "AllOnes" || n == "Nonzero") && arg.kind == BS_LITERAL_VALUE && number(arg.integer, arg, bits))
                    return (n == "AllOnes" ? bits == mask(arg.width) : bits != 0) ? BS_TRUE : BS_FALSE;
            }
            return BS_UNKNOWN;
        };
        a.literal = [](void* c, const char* s, BSValue source, const char* rule) -> BSValue {
            return protect(c, BSValue{0}, [&](Adapter& a) { return handle(a.literal(s, term(source), rule)); });
        };
        a.binary = [](void* c, const char* op, BSValue x, BSValue y, BSValue source, const char* rule) -> BSValue {
            return protect(c, BSValue{0}, [&](Adapter& a) { return handle(a.binary(op, &term(x), &term(y), term(source), rule)); });
        };
        a.rebuild = [](void* c, BSValue original, BSValue x, BSValue y) -> BSValue {
            return protect(c, BSValue{0}, [&](Adapter& a) {
                const auto& old = term(original);
                if (old.left == &term(x) && old.right == &term(y)) return original;
                // Preserve the original semantics, including flags, on a rebuilt node.
                Term next = old; next.left = &term(x); next.right = &term(y); next.binding.reset();
                return handle(a.add(std::move(next)));
            });
        };
        a.fold = [](void* c, BSValue v, const char* rule) -> BSValue {
            return protect(c, BSValue{0}, [&](Adapter& a) { return handle(a.fold(term(v), rule)); });
        };
        a.trace = [](void* c, const BSTrace* trace) {
            protect(c, false, [&](Adapter& a) {
                std::string source;
                if (trace->rule_source.path) source = std::string(trace->rule_source.path) + ":" + std::to_string(trace->rule_source.line) + ":" + std::to_string(trace->rule_source.column);
                a.trace.push_back({term(trace->before).provenance, trace->rule, 1, std::move(source)}); return true;
            });
        };
        return a;
    }
    Term* optimize(const Kernel& kernel, Term* root, std::size_t& budget, DiagnosticEngine& d) {
        auto a = callbacks(); BSRewriteOptions options{budget, BS_MAX_DEPTH}; BSRewriteStats stats{};
        BSValue out = 0; BSError error{};
        auto status = bs_optimize(kernel.profile(), &a, handle(root), &options, &out, &stats, &error);
        if (status != BS_OK || failed) { d.error("sched.rewrite", failed ? "Blackstone adapter construction failed" : error.message); return nullptr; }
        budget -= stats.rewrites; return &term(out);
    }
};
std::string opcode(std::string_view name) {
    auto suffix = name.substr(name.find('.') + 1);
    if (suffix == "addi" || suffix == "add") return "plus";
    if (suffix == "subi" || suffix == "sub") return "minus";
    if (suffix == "muli" || suffix == "mul") return "times";
    if (suffix == "divsi" || suffix == "divui" || suffix == "div") return "divide";
    if (suffix == "and") return "bit_and";
    if (suffix == "or") return "bit_or";
    if (suffix == "xor") return "bit_xor";
    return {};
}
std::string operation(const Term& t, std::string_view dialect = "arith") {
    std::string suffix;
    if (t.kind == BS_LITERAL_VALUE) suffix = "constant";
    else if (t.op == "plus") suffix = dialect == "arith" ? "addi" : "add";
    else if (t.op == "minus") suffix = dialect == "arith" ? "subi" : "sub";
    else if (t.op == "times") suffix = dialect == "arith" ? "muli" : "mul";
    else if (t.op == "divide") suffix = dialect == "arith" ? (t.signed_ ? "divsi" : "divui") : "div";
    else suffix = t.op.substr(4);
    return std::string(dialect) + "." + suffix;
}
Term type(const Module& module, ValueId id) {
    Term t; auto* v = module.value(id); if (!v) throw std::runtime_error("missing SSA value");
    t.type = v->type; t.binding = id; auto s = module.context->spelling(v->type);
    if (s.size() > 1 && (s[0] == 'i' || s[0] == 'u')) {
        auto r = std::from_chars(s.data() + 1, s.data() + s.size(), t.width);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || t.width > 64) t.width = 0;
        t.signed_ = s[0] == 'i';
    } else { t.domain = std::string(s); t.width = 1; }
    return t;
}
firestone::ir::Opcode fire_opcode(std::string_view op) {
    using O = firestone::ir::Opcode;
    if (op == "plus") return O::Add;
    if (op == "minus") return O::Sub;
    if (op == "times") return O::Mul;
    if (op == "divide") return O::Div;
    if (op == "bit_and") return O::And;
    if (op == "bit_or") return O::Or;
    if (op == "bit_xor") return O::Xor;
    throw std::runtime_error("unsupported Firestone expression operator");
}
std::string black_opcode(firestone::ir::Opcode op) {
    using O = firestone::ir::Opcode;
    switch (op) {
    case O::Add: return "plus"; case O::Sub: return "minus"; case O::Mul: return "times";
    case O::Div: return "divide"; case O::And: return "bit_and"; case O::Or: return "bit_or";
    case O::Xor: return "bit_xor"; default: return {};
    }
}
}
bool apply(const Kernel& kernel, Module& module, std::size_t budget, std::vector<Event>& events, DiagnosticEngine& d, const Facts& facts) {
    if (!verify(module, d)) return false;
    auto candidate = module; Adapter adapter; std::map<ValueId, Term*> terms;
    auto lookup = [&](ValueId id) -> Term* {
        auto [it, inserted] = terms.emplace(id, nullptr);
        if (inserted) {
            auto t = type(candidate, id); t.pure = BS_TRUE;
            if (facts) { t.pure = facts(id, "Pure"); t.total = facts(id, "Total"); }
            it->second = adapter.add(std::move(t));
        }
        return it->second;
    };
    std::map<ValueId, ValueId> replacements;
    std::function<bool(Region&)> rewrite = [&](Region& region) {
        for (auto& block : region.blocks) {
            auto original = std::move(block.operations); block.operations.clear();
            for (auto& op : original) {
                for (auto& child : op.regions) if (!rewrite(child)) return false;
                block.operations.push_back(op);
                if (op.results.size() != 1 || !op.regions.empty()) continue;
                // Dead definitions are retained for host effect semantics, but cannot
                // contribute a replacement use. Revisiting them would grow dead IR.
                if (candidate.value(op.results[0])->uses.empty()) continue;
                auto id = op.results[0]; auto t = type(candidate, id); t.provenance = std::to_string(op.id);
                t.attributes = op.attributes;
                auto prefix = op.name.substr(0, op.name.find('.'));
                bool supported = prefix == "arith" || prefix == "high" || prefix == "mid" || prefix == "low" ||
                    prefix == "machine" || prefix == "interp" || prefix == "llvmir" || prefix == "wasm" || prefix == "gpu";
                for (const auto& [key, value] : op.attributes) {
                    (void)value;
                    if (key != "value" && key != "origin") t.flags = true;
                }
                if (supported && op.name.ends_with(".constant") && op.attributes.contains("value") && t.width) {
                    // MLVM iN constants also admit unsigned bit-pattern spelling.
                    Term unsigned_type = t; unsigned_type.signed_ = false; std::uint64_t bits;
                    if (!number(op.attributes.at("value"), t, bits) && !number(op.attributes.at("value"), unsigned_type, bits)) continue;
                    t.kind = BS_LITERAL_VALUE; t.integer = decimal(bits, t); t.pure = t.total = BS_TRUE;
                } else if (supported && op.operands.size() == 2 && !(t.op = opcode(op.name)).empty()) {
                    // Signless arith.divui is not interchangeable with signed division.
                    if (op.name == "arith.divui") continue;
                    t.kind = BS_BINARY_VALUE; t.left = lookup(op.operands[0]); t.right = lookup(op.operands[1]);
                    t.pure = t.left->pure == BS_TRUE && t.right->pure == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
                    t.total = t.op != "divide" && !t.flags && t.left->total == BS_TRUE && t.right->total == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
                } else continue;
                auto* root = adapter.add(std::move(t)); terms[id] = root;
                auto* optimized = adapter.optimize(kernel, root, budget, d); if (!optimized) return false;
                std::map<Term*, ValueId> emitted;
                std::function<ValueId(Term*)> materialize = [&](Term* value) -> ValueId {
                    if (value->binding) return *value->binding;
                    if (emitted.contains(value)) return emitted.at(value);
                    std::vector<ValueId> operands;
                    if (value->kind == BS_BINARY_VALUE) operands = {materialize(value->left), materialize(value->right)};
                    Identifier result_type = static_cast<Identifier>(value->type);
                    auto& made = candidate.append(block, operation(*value, prefix), operands, {&result_type, 1}, op.location);
                    if (value->kind == BS_LITERAL_VALUE) made.attributes["value"] = value->integer;
                    if (value->flags) made.attributes = value->attributes;
                    made.attributes["origin"] = value->origin;
                    return emitted[value] = made.results[0];
                };
                auto replacement = materialize(optimized);
                if (replacement != id) replacements[id] = replacement;
                terms[id] = optimized;
            }
        }
        return true;
    };
    try {
        if (!rewrite(candidate.body)) return false;
        walk(candidate, [&](Operation& op) {
            for (auto& operand : op.operands) { std::set<ValueId> seen; while (replacements.contains(operand) && seen.insert(operand).second) operand = replacements.at(operand); }
        });
        candidate.rebuild_uses(); if (!verify(candidate, d)) return false;
        auto committed_events = events;
        committed_events.insert(committed_events.end(), adapter.trace.begin(), adapter.trace.end());
        module = std::move(candidate); events = std::move(committed_events); return true;
    } catch (const std::exception& e) { d.error("sched.adapter", e.what()); return false; }
}
bool apply(const Kernel& kernel, Dataflow& data, std::size_t budget, std::vector<Event>& events, DiagnosticEngine& d) {
    if (!verify_artifact({"firestone.dataflow", data}, d)) return false;
    try {
        Adapter a; std::map<std::size_t, Term*> terms; std::map<std::string, uintptr_t> type_names;
        for (auto id : data.graph.schedule()) {
            const auto& inst = data.graph.node(id); Term t;
            t.width = inst.type.bits; t.signed_ = data.is_signed; t.binding = id; t.provenance = inst.annotation;
            t.fire_type = inst.type;
            auto [type_name, inserted] = type_names.emplace(inst.type.name, type_names.size() + 1);
            (void)inserted; t.type = type_name->second;
            t.domain = inst.type.kind == firestone::ir::TypeKind::Integer ? "integer" : "other";
            if (inst.type == firestone::ir::Type::boolean()) t.domain = "boolean";
            if (!t.width) t.width = 1;
            if (inst.opcode == firestone::ir::Opcode::Constant && inst.operands[0].kind == firestone::ir::OperandKind::Integer && t.domain == "integer") {
                t.kind = BS_LITERAL_VALUE; t.integer = std::to_string(inst.operands[0].integer); t.pure = t.total = BS_TRUE;
                std::uint64_t bits;
                if (!number(t.integer, t, bits)) throw std::runtime_error("Firestone literal is not representable in the selected signedness/width");
            } else if (!(t.op = black_opcode(inst.opcode)).empty()) {
                t.kind = BS_BINARY_VALUE; t.left = terms.at(inst.operands[0].value); t.right = terms.at(inst.operands[1].value);
                t.pure = t.left->pure == BS_TRUE && t.right->pure == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
                t.total = t.op != "divide" && t.left->total == BS_TRUE && t.right->total == BS_TRUE ? BS_TRUE : BS_UNKNOWN;
            } else { t.pure = BS_TRUE; t.total = BS_UNKNOWN; }
            terms[id] = a.add(std::move(t));
        }
        // Retain already-produced definitions, including potentially trapping division.
        // Only uses/roots change; the host decides when dead definitions may be erased.
        Dataflow result = data; result.roots.clear(); std::map<Term*, std::size_t> built;
        std::function<std::size_t(Term*)> emit = [&](Term* t) -> std::size_t {
            if (t->binding) return *t->binding;
            if (built.contains(t)) return built.at(t);
            auto type = t->fire_type; std::size_t id;
            if (t->kind == BS_LITERAL_VALUE) {
                std::int64_t value = 0; auto r = std::from_chars(t->integer.data(), t->integer.data() + t->integer.size(), value);
                if (r.ec != std::errc{}) throw std::runtime_error("Firestone literal exceeds its int64 storage");
                id = result.graph.constant(value, type);
            } else if (t->kind == BS_BINARY_VALUE) id = result.graph.operation(fire_opcode(t->op), type, {emit(t->left), emit(t->right)});
            else {
                if (!t->binding) throw std::runtime_error("unbound Firestone input");
                const auto& old = data.graph.node(*t->binding);
                if (old.opcode == firestone::ir::Opcode::Constant && old.operands[0].kind == firestone::ir::OperandKind::Symbol)
                    id = result.graph.input(old.operands[0].symbol, old.type);
                else {
                    std::vector<std::size_t> args;
                    for (const auto& arg : old.operands) args.push_back(emit(terms.at(arg.value)));
                    id = result.graph.operation(old.opcode, old.type, args);
                }
            }
            return built[t] = id;
        };
        for (auto root : data.roots) {
            auto* optimized = a.optimize(kernel, terms.at(root), budget, d); if (!optimized) return false;
            result.roots.push_back(emit(optimized));
        }
        if (!verify_artifact({"firestone.dataflow", result}, d)) return false;
        auto committed_events = events;
        committed_events.insert(committed_events.end(), a.trace.begin(), a.trace.end());
        data = std::move(result); events = std::move(committed_events); return true;
    } catch (const std::exception& e) { d.error("sched.adapter", e.what()); return false; }
}
} // namespace mlvm::sched
