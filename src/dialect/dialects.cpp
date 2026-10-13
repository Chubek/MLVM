#include <mlvm/dialect.hpp>
#include <charconv>
#include <limits>

namespace mlvm {
namespace {
std::string_view value_type(const Module& m, ValueId id) {
    const auto* v = m.value(id); return v ? m.context->spelling(v->type) : std::string_view{};
}
unsigned width(std::string_view t) {
    unsigned n = 0;
    if (t.size() < 2 || t[0] != 'i') return 0;
    auto r = std::from_chars(t.data() + 1, t.data() + t.size(), n);
    return r.ec == std::errc{} && r.ptr == t.data() + t.size() && n <= 64 ? n : 0;
}
bool fail(const Operation& op, DiagnosticEngine& d, std::string message) {
    d.error("dialect." + op.name, std::move(message), op.location); return false;
}
bool integer(const Operation& op, const Module& m, DiagnosticEngine& d) {
    if (op.results.size() != 1) return fail(op, d, "requires one integer result");
    auto t = value_type(m, op.results[0]); auto bits = width(t);
    if (!bits) return fail(op, d, "requires i1..i64 result type");
    if (op.name.starts_with("wasm.") && bits != 32 && bits != 64) return fail(op, d, "WebAssembly integers must be i32 or i64");
    for (auto id : op.operands) if (value_type(m, id) != t) return fail(op, d, "operand and result types must agree");
    if (op.name.starts_with("wasm.") && (op.attributes.contains("flags") || op.attributes.contains("nsw") || op.attributes.contains("nuw")))
        return fail(op, d, "WebAssembly arithmetic does not carry overflow flags");
    if (op.name.ends_with(".constant")) {
        auto it = op.attributes.find("value"); std::int64_t value = 0;
        if (it == op.attributes.end()) return fail(op, d, "constant requires a decimal value");
        const auto& s = it->second; auto r = std::from_chars(s.data(), s.data() + s.size(), value);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() ||
            (bits < 64 && (value < -(std::int64_t{1} << (bits - 1)) ||
             (value >= 0 && static_cast<std::uint64_t>(value) > ((std::uint64_t{1} << bits) - 1)))))
            return fail(op, d, "constant is not representable in its result type");
    }
    return true;
}
bool select(const Operation& op, const Module& m, DiagnosticEngine& d) {
    const auto condition = op.name.starts_with("wasm.") ? "i32" : "i1";
    if (op.operands.size() != 3 || op.results.size() != 1 || value_type(m, op.operands[0]) != condition ||
        value_type(m, op.operands[1]) != value_type(m, op.results[0]) || value_type(m, op.operands[2]) != value_type(m, op.results[0]))
        return fail(op, d, std::string("select requires ") + condition + " condition and two values matching its result");
    if (op.name.starts_with("wasm.") && value_type(m, op.results[0]) != "i32" && value_type(m, op.results[0]) != "i64")
        return fail(op, d, "WebAssembly select requires an i32 or i64 result");
    return true;
}
bool memory(const Operation& op, const Module& m, DiagnosticEngine& d) {
    if (op.operands.empty() || value_type(m, op.operands[0]) != "ptr") return fail(op, d, "memory address must have type ptr");
    if (auto it = op.attributes.find("alignment"); it != op.attributes.end()) {
        std::uint64_t n = 0; auto& s = it->second; auto r = std::from_chars(s.data(), s.data() + s.size(), n);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !n || (n & (n - 1))) return fail(op, d, "alignment must be a positive power of two");
    }
    return true;
}
bool symbol(const Operation& op, const Module&, DiagnosticEngine& d) {
    auto it = op.attributes.find("callee");
    return it != op.attributes.end() && !it->second.empty() ? true : fail(op, d, "requires a nonempty callee attribute");
}
}
bool register_standard_dialects(Context& c, DiagnosticEngine& d) {
    bool ok = true;
    for (auto name : {"cf", "index", "mem", "high", "mid", "low", "machine", "interp", "llvmir", "wasm", "gpu"})
        if (!c.has_dialect(name)) ok = c.register_dialect(name, d) && ok;
    auto add = [&](OperationDefinition def) { if (!c.operation(def.name)) ok = c.register_operation(std::move(def), d) && ok; };
    for (std::string prefix : {"high", "mid", "low", "machine", "interp", "llvmir", "wasm", "gpu"}) {
        add({prefix + ".constant", 0, 0, 1, 1, 0, 0, Effect::pure, false, integer});
        for (std::string op : {"add", "sub", "mul", "div", "and", "or", "xor"})
            add({prefix + "." + op, 2, 2, 1, 1, 0, 0, op == "div" ? Effect::unknown : Effect::pure, false, integer});
        add({prefix + ".select", 3, 3, 1, 1, 0, 0, Effect::pure, false, select});
    }
    for (std::string prefix : {"mem", "low", "llvmir"}) {
        add({prefix + ".load", 1, 1, 1, 1, 0, 0, Effect::read, false, memory});
        add({prefix + ".store", 2, 2, 0, 0, 0, 0, Effect::write, false, memory});
    }
    add({"index.constant", 0, 0, 1, 1, 0, 0, Effect::pure, false, [](const Operation& op, const Module& m, DiagnosticEngine& d) {
        auto it = op.attributes.find("value"); std::uint64_t n = 0;
        if (op.results.size() != 1 || value_type(m, op.results[0]) != "index" || it == op.attributes.end()) return fail(op, d, "index constant requires index result and nonnegative value");
        auto& s = it->second; auto r = std::from_chars(s.data(), s.data() + s.size(), n);
        return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? true : fail(op, d, "invalid index constant");
    }});
    add({"index.cast", 1, 1, 1, 1, 0, 0, Effect::pure, false, [](const Operation& op, const Module& m, DiagnosticEngine& d) {
        if (op.operands.size() != 1 || op.results.size() != 1) return false;
        auto a = value_type(m, op.operands[0]), b = value_type(m, op.results[0]);
        return (a == "index" && width(b)) || (b == "index" && width(a)) ? true : fail(op, d, "index.cast converts between index and an integer");
    }});
    add({"cf.unreachable", 0, 0, 0, 0, 0, 0, Effect::unknown, true, {}});
    add({"cf.assert", 1, 1, 0, 0, 0, 0, Effect::unknown, false, [](const Operation& op, const Module& m, DiagnosticEngine& d) {
        return op.operands.size() == 1 && value_type(m, op.operands[0]) == "i1" ? true : fail(op, d, "assert condition must be i1");
    }});
    add({"high.yield", 0, std::numeric_limits<std::size_t>::max(), 0, 0, 0, 0, Effect::unknown, true, {}});
    add({"high.if", 1, 1, 0, std::numeric_limits<std::size_t>::max(), 2, 2, Effect::unknown, false,
        [](const Operation& op, const Module& m, DiagnosticEngine& d) {
            if (op.operands.size() != 1 || value_type(m, op.operands[0]) != "i1" || op.regions.size() != 2) return fail(op, d, "high.if requires i1 condition and two regions");
            for (const auto& r : op.regions) {
                if (r.blocks.size() != 1 || !r.blocks[0].arguments.empty() || r.blocks[0].operations.empty()) return fail(op, d, "if arms require one nonempty block without arguments");
                const auto& yield = r.blocks[0].operations.back();
                if (yield.name != "high.yield" || yield.operands.size() != op.results.size()) return fail(op, d, "if arms must yield every result");
                for (std::size_t i = 0; i < op.results.size(); ++i) if (value_type(m, yield.operands[i]) != value_type(m, op.results[i])) return fail(op, d, "if yield type mismatch");
            }
            return true;
        }});
    add({"low.cast", 1, 1, 1, 1, 0, 0, Effect::pure, false,
        [](const Operation& op, const Module& m, DiagnosticEngine& d) {
            if (op.operands.size() != 1 || op.results.size() != 1) return false;
            auto a = width(value_type(m, op.operands[0])), b = width(value_type(m, op.results[0]));
            auto it = op.attributes.find("kind");
            if (!a || !b || it == op.attributes.end()) return fail(op, d, "cast requires integer types and kind=trunc|sext|zext");
            return (it->second == "trunc" && a > b) || ((it->second == "sext" || it->second == "zext") && a < b) ? true : fail(op, d, "cast kind disagrees with widths");
        }});
    add({"machine.copy", 1, 1, 1, 1, 0, 0, Effect::pure, false, integer});
    add({"interp.call", 0, std::numeric_limits<std::size_t>::max(), 0, 1, 0, 0, Effect::unknown, false, symbol});
    add({"interp.guard", 1, 1, 0, 0, 0, 0, Effect::unknown, false,
        [](const Operation& op, const Module& m, DiagnosticEngine& d) {
            return op.operands.size() == 1 && value_type(m, op.operands[0]) == "i1" && op.attributes.contains("deopt") && !op.attributes.at("deopt").empty() ? true : fail(op, d, "guard requires i1 condition and deopt continuation");
        }});
    add({"wasm.drop", 1, 1, 0, 0, 0, 0, Effect::pure, false, [](const Operation& op, const Module& m, DiagnosticEngine& d) {
        auto t = op.operands.size() == 1 ? value_type(m, op.operands[0]) : "";
        return t == "i32" || t == "i64" ? true : fail(op, d, "drop requires a WebAssembly integer");
    }});
    add({"gpu.thread_id", 0, 0, 1, 1, 0, 0, Effect::pure, false,
        [](const Operation& op, const Module& m, DiagnosticEngine& d) {
            auto it = op.attributes.find("axis");
            return op.results.size() == 1 && value_type(m, op.results[0]) == "i32" && it != op.attributes.end() &&
                (it->second == "x" || it->second == "y" || it->second == "z") ? true : fail(op, d, "thread_id requires i32 result and axis=x|y|z");
        }});
    add({"gpu.barrier", 0, 0, 0, 0, 0, 0, Effect::unknown, false, {}});
    add({"gpu.launch", 6, std::numeric_limits<std::size_t>::max(), 0, 0, 0, 0, Effect::unknown, false,
        [](const Operation& op, const Module& m, DiagnosticEngine& d) {
            if (!symbol(op, m, d) || op.operands.size() < 6) return false;
            for (std::size_t i = 0; i < 6; ++i) if (value_type(m, op.operands[i]) != "index") return fail(op, d, "launch requires six index grid/block dimensions before kernel arguments");
            return true;
        }});
    return ok;
}
ConversionGraph standard_conversions() {
    ConversionGraph graph;
    for (auto [source, target] : {std::pair{"arith", "high"}, {"high", "mid"}, {"mid", "low"},
        {"low", "machine"}, {"low", "interp"}, {"low", "llvmir"}, {"low", "wasm"}, {"low", "gpu"}}) {
        graph.add({source, target, [to = std::string(target)](Operation& op, Module&, DiagnosticEngine& d) {
            auto name = op.name.substr(op.name.find('.') + 1);
            if (name == "addi") name = "add"; else if (name == "subi") name = "sub";
            else if (name == "muli") name = "mul"; else if (name == "divsi") name = "div";
            if (name != "constant" && name != "add" && name != "sub" && name != "mul" && name != "div" &&
                name != "and" && name != "or" && name != "xor" && name != "select")
                return fail(op, d, "operation has no standard expression lowering");
            op.name = to + "." + name; return true;
        }});
    }
    return graph;
}
bool lower_dialect(Module& module, std::string_view dialect, DiagnosticEngine& d) {
    if (!module.context->has_dialect(dialect)) { d.error("dialect.unknown", "unknown target dialect: " + std::string(dialect)); return false; }
    return standard_conversions().convert(module, {"builtin", "func", std::string(dialect)}, d);
}
} // namespace mlvm
