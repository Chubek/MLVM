#include <mlvm/c.hpp>
#include <charconv>

namespace mlvm {
namespace {
// The C dialect is target-neutral: only integer widths and the pointer type
// carry meaning. Sizes and signedness live in explicit operation attributes.
bool integer_or_pointer(std::string_view spelling) {
    if (spelling == "ptr") return true;
    if (spelling.size() < 2 || spelling[0] != 'i') return false;
    unsigned width = 0;
    const auto result = std::from_chars(spelling.data() + 1, spelling.data() + spelling.size(), width);
    return result.ec == std::errc{} && result.ptr == spelling.data() + spelling.size() &&
           (width == 8 || width == 16 || width == 32 || width == 64);
}
std::optional<unsigned> attribute_number(const Operation& op, std::string_view key) {
    auto it = op.attributes.find(key);
    if (it == op.attributes.end()) return std::nullopt;
    const auto& s = it->second;
    unsigned value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) return std::nullopt;
    return value;
}
std::string_view attribute(const Operation& op, std::string_view key) {
    auto it = op.attributes.find(key);
    return it == op.attributes.end() ? std::string_view{} : std::string_view{it->second};
}
bool type_attribute(const Operation& op, std::string_view key) {
    auto it = op.attributes.find(key);
    return it != op.attributes.end() && integer_or_pointer(it->second);
}
} // namespace

bool register_c_dialect(Context& context, DiagnosticEngine& diagnostics) {
    bool ok = context.has_dialect("c") || context.register_dialect("c", diagnostics);
    auto verify = [](const Operation& op, const Module& module, DiagnosticEngine& d) {
        auto fail = [&](std::string message) { d.error("c.verify", std::move(message), op.location); return false; };
        for (auto id : op.operands) {
            const auto* value = module.value(id);
            if (!value || !integer_or_pointer(module.context->spelling(value->type)))
                return fail("C operands must have integer or pointer type");
        }
        for (auto id : op.results) {
            const auto* value = module.value(id);
            if (!value || !integer_or_pointer(module.context->spelling(value->type)))
                return fail("C results must have integer or pointer type");
        }
        if (op.name == "c.load" || op.name == "c.store") {
            const auto slot = attribute_number(op, "slot");
            if (!slot || *slot >= 65536)
                return fail("local access requires a valid slot");
            if (op.name == "c.load") {
                const auto type = op.attributes.find("type");
                if (op.results.size() != 1 || type == op.attributes.end() || !type_attribute(op, "type"))
                    return fail("c.load requires the loaded type");
                const auto* result = module.value(op.results[0]);
                if (!result || module.context->spelling(result->type) != type->second)
                    return fail("c.load type must match its result");
            }
        }
        if (op.name == "c.address_of") {
            const auto kind = attribute(op, "kind");
            if (kind.empty()) return fail("c.address_of requires a kind");
            if (kind == "local") {
                const auto bytes = attribute_number(op, "bytes");
                const auto slot = attribute_number(op, "slot");
                if (!slot || !bytes || *bytes == 0 || *bytes > (1u << 24))
                    return fail("local addresses require a slot and a valid byte size");
            } else if (kind == "global") {
                if (attribute(op, "sym").empty()) return fail("global addresses require a symbol");
            } else return fail("c.address_of kind must be local or global");
        }
        if (op.name == "c.gep") {
            const auto scale = attribute_number(op, "scale");
            if (!scale || *scale == 0 || *scale > 4096) return fail("c.gep requires a valid element scale");
            if (op.operands.size() != 2) return fail("c.gep requires a pointer base and a 64-bit index");
            const auto* base = module.value(op.operands[0]);
            const auto* index = module.value(op.operands[1]);
            if (!base || !index || module.context->spelling(base->type) != "ptr" ||
                module.context->spelling(index->type) != "i64")
                return fail("c.gep requires a pointer base and a 64-bit index");
        }
        if (op.name == "c.cast") {
            if (op.results.size() != 1 || !type_attribute(op, "from") || !type_attribute(op, "to"))
                return fail("c.cast requires from/to types");
            const auto* operand = op.operands.empty() ? nullptr : module.value(op.operands[0]);
            const auto* result = module.value(op.results[0]);
            if (!operand || !result || module.context->spelling(operand->type) != attribute(op, "from") ||
                module.context->spelling(result->type) != attribute(op, "to"))
                return fail("c.cast from/to types must match its operand and result");
            const auto sign = attribute(op, "signed");
            if (sign != "true" && sign != "false") return fail("c.cast requires explicit signedness");
        }
        if (op.name == "c.global") {
            const auto size = attribute_number(op, "size");
            const auto align = attribute_number(op, "align");
            if (attribute(op, "sym").empty()) return fail("c.global requires a symbol");
            if (!size || *size == 0 || *size > (1u << 24)) return fail("c.global requires a valid size");
            if (!align || *align == 0 || (*align & (*align - 1)) != 0 || *align > 4096)
                return fail("c.global requires a valid alignment");
            const auto section = attribute(op, "section");
            if (section != "data" && section != "bss" && section != "rodata")
                return fail("c.global section must be data, bss, or rodata");
        }
        if (op.name == "c.call" || op.name == "c.declare") {
            if (attribute(op, "sym").empty()) return fail("call/declaration requires a symbol");
        }
        if (op.name == "c.ptrload") {
            const auto type = op.attributes.find("type");
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (type == op.attributes.end() || !type_attribute(op, "type") || !result ||
                module.context->spelling(result->type) != type->second)
                return fail("c.ptrload type must match its result");
        }
        if (op.name == "c.shl" || op.name == "c.shr") {
            const auto* left = op.operands.empty() ? nullptr : module.value(op.operands[0]);
            const auto* right = op.operands.size() < 2 ? nullptr : module.value(op.operands[1]);
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (!left || !right || !result || left->type != right->type || left->type != result->type ||
                module.context->spelling(left->type) == "ptr")
                return fail("C shifts require matching integer operand and result types");
        }
        if (op.name == "c.lt" || op.name == "c.le" || op.name == "c.gt" || op.name == "c.ge" ||
            op.name == "c.eq" || op.name == "c.ne") {
            const auto* left = op.operands.empty() ? nullptr : module.value(op.operands[0]);
            const auto* right = op.operands.size() < 2 ? nullptr : module.value(op.operands[1]);
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (!left || !right || !result || left->type != right->type ||
                module.context->spelling(result->type) != "i32")
                return fail("C comparisons require matching operands and an i32 result");
        }
        if (op.name == "c.address_of" && attribute(op, "kind") == "local") {
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (!result || module.context->spelling(result->type) != "ptr")
                return fail("local addresses must produce ptr");
        }
        if (op.name == "c.address_of" && attribute(op, "kind") == "global") {
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (!result || module.context->spelling(result->type) != "ptr")
                return fail("global addresses must produce ptr");
        }
        if (op.name == "c.gep") {
            const auto* result = op.results.empty() ? nullptr : module.value(op.results[0]);
            if (!result || module.context->spelling(result->type) != "ptr")
                return fail("c.gep must produce ptr");
        }
        if (op.name == "c.if" || op.name == "c.select") {
            const auto* condition = op.operands.empty() ? nullptr : module.value(op.operands[0]);
            if (!condition || module.context->spelling(condition->type) != "i32")
                return fail("C control conditions must have type i32");
        }
        for (std::size_t i = 0; i < op.regions.size(); ++i) {
            const auto& region = op.regions[i];
            if (region.blocks.size() != 1 || !region.blocks[0].arguments.empty())
                return fail("C control region requires one argument-free block");
            const auto& operations = region.blocks[0].operations;
            if (operations.empty()) return fail("empty C control region");
            const auto& end = operations.back();
            const auto arity = op.name == "c.select" || (op.name == "c.loop" && i == 0) ? 1U : 0U;
            if (end.name == "c.yield" && end.operands.size() != arity) return fail("incorrect control-region yield arity");
            if (arity && end.name != "c.yield") return fail("value region requires c.yield");
        }
        return true;
    };
    auto add = [&](const char* name, unsigned operands, unsigned results, unsigned regions,
                   Effect effect = Effect::unknown, bool terminator = false) {
        if (!context.operation(name)) ok = context.register_operation(
            {name, operands, operands, results, results, regions, regions, effect, terminator, verify}, diagnostics) && ok;
    };
    add("c.load", 0, 1, 0, Effect::read);
    add("c.store", 1, 0, 0, Effect::write);
    add("c.if", 1, 0, 2); add("c.select", 1, 1, 2); add("c.loop", 0, 0, 3);
    add("c.break", 0, 0, 0, Effect::unknown, true);
    add("c.continue", 0, 0, 0, Effect::unknown, true);
    add("c.declare", 0, 0, 0);
    add("c.global", 0, 0, 0);
    add("c.address_of", 0, 1, 0);
    add("c.gep", 2, 1, 0);
    add("c.ptrload", 1, 1, 0, Effect::read);
    add("c.ptrstore", 2, 0, 0, Effect::write);
    for (const char* name : {"c.cast"}) add(name, 1, 1, 0, Effect::pure);
    for (const char* name : {"c.shl", "c.shr", "c.lt", "c.le", "c.gt", "c.ge", "c.eq", "c.ne"})
        add(name, 2, 1, 0, Effect::pure);
    if (!context.operation("c.yield")) ok = context.register_operation(
        {"c.yield", 0, 1, 0, 0, 0, 0, Effect::unknown, true, verify}, diagnostics) && ok;
    if (!context.operation("c.call")) ok = context.register_operation(
        {"c.call", 0, 6, 0, 1, 0, 0, Effect::unknown, false, verify}, diagnostics) && ok;
    return ok;
}

bool verify_c_module(const Module& module, DiagnosticEngine& diagnostics) {
    bool valid = true;
    auto fail = [&](const Operation& op, std::string message) {
        diagnostics.error("c.contract", std::move(message), op.location); valid = false;
    };
    auto number = [](const Operation& op, const char* key) -> std::optional<unsigned> {
        return attribute_number(op, key);
    };
    // Global slots are module-scoped: a local address may only name a frame
    // declared by its function. Global addresses may refer to definitions
    // from other translation units, which only the linker resolves, so the
    // module verifier does not require an in-module definition.
    std::map<std::string, unsigned> globals;
    for (const auto& block : module.body.blocks) for (const auto& op : block.operations) {
        if (op.name != "c.global") continue;
        auto size = number(op, "size");
        const auto symbol = op.attributes.find("sym");
        if (!size || symbol == op.attributes.end() || symbol->second.empty()) continue;
        if (!globals.emplace(symbol->second, *size).second)
            fail(op, "duplicate global symbol '" + op.attributes.at("sym") + "'");
    }
    struct Signature { std::size_t arguments; std::string result; };
    std::map<std::string, Signature> signatures;
    for (const auto& block : module.body.blocks) for (const auto& op : block.operations) {
        if (op.name != "builtin.func" && op.name != "c.declare") continue;
        if (!op.attributes.contains("sym")) continue;
        auto arguments = op.name == "c.declare" ? number(op, "parameters") : std::optional<unsigned>{};
        if (op.name == "builtin.func" && op.regions.size() == 1 && op.regions[0].blocks.size() == 1)
            arguments = static_cast<unsigned>(op.regions[0].blocks[0].arguments.size());
        if (!arguments.has_value()) { fail(op, "missing function parameter count"); continue; }
        const auto result = op.attributes.contains("result") ? op.attributes.at("result") : "void";
        if (op.name == "c.declare" && (*arguments > 6 || (!integer_or_pointer(result) && result != "void")))
            fail(op, "C declaration requires up to six parameters and an integer, pointer, or void result");
        Signature signature{*arguments, result};
        auto [it, added] = signatures.emplace(op.attributes.at("sym"), signature);
        if (!added && (it->second.arguments != signature.arguments || it->second.result != signature.result))
            fail(op, "incompatible function declarations");
    }
    std::function<void(const Region&, const Operation*, const Operation*, unsigned, unsigned, unsigned)> visit;
    visit = [&](const Region& region, const Operation* function, const Operation* parent, unsigned region_index, unsigned loops, unsigned depth) {
        if (depth > 128) return; // The generic verifier diagnoses excessive depth.
        for (const auto& block : region.blocks) for (const auto& op : block.operations) {
            if (op.name.starts_with("c.") && op.name != "c.declare" && op.name != "c.global" && !function)
                fail(op, "C operation must be inside a function");
            if ((op.name == "c.declare" || op.name == "c.global") && parent)
                fail(op, "C declaration and global definitions must be at module scope");
            if (op.name == "c.load" || op.name == "c.store") {
                auto slot = number(op, "slot");
                auto count = function ? number(*function, "locals") : std::nullopt;
                if (!slot.has_value() || !count.has_value() || *count > 65536 || *slot >= *count) fail(op, "local access is outside its function frame");
            }
            if (op.name == "c.address_of" && function) {
                auto kind = op.attributes.find("kind");
                if (kind != op.attributes.end() && kind->second == "local") {
                    auto slot = number(op, "slot");
                    auto count = number(*function, "locals");
                    if (!slot.has_value() || !count.has_value() || *slot >= *count) fail(op, "frame address is outside its function frame");
                }
            }
            if (op.name == "c.ptrstore" && op.operands.size() == 2) {
                const auto* address = module.value(op.operands[1]);
                if (!address || module.context->spelling(address->type) != "ptr") fail(op, "c.ptrstore requires a pointer destination");
            }
            if (op.name == "c.ptrload" && op.operands.size() == 1) {
                const auto* address = module.value(op.operands[0]);
                if (!address || module.context->spelling(address->type) != "ptr") fail(op, "c.ptrload requires a pointer source");
            }
            if ((op.name == "c.break" || op.name == "c.continue") && !loops) fail(op, "loop transfer outside a loop body");
            if (op.name == "c.yield") {
                if (!parent || (parent->name != "c.if" && parent->name != "c.select" && parent->name != "c.loop"))
                    fail(op, "yield must terminate a C control region");
                else {
                    auto arity = parent->name == "c.select" || (parent->name == "c.loop" && region_index == 0) ? 1U : 0U;
                    if (op.operands.size() != arity) fail(op, "incorrect C yield arity");
                }
            }
            if (op.name == "c.call" && op.attributes.contains("sym")) {
                auto signature = signatures.find(op.attributes.at("sym"));
                if (signature == signatures.end()) fail(op, "call requires a function declaration or definition");
                else if (signature->second.arguments != op.operands.size() ||
                         op.results.size() != (signature->second.result == "void" ? 0U : 1U) ||
                         (!op.results.empty() && (!module.value(op.results[0]) ||
                             module.context->spelling(module.value(op.results[0])->type) != signature->second.result)))
                    fail(op, "C call signature mismatch");
            }
            if (op.name == "c.loop" && (!op.attributes.contains("kind") ||
                (op.attributes.at("kind") != "while" && op.attributes.at("kind") != "do"))) fail(op, "invalid C loop kind");
            for (unsigned i = 0; i < op.regions.size(); ++i)
                visit(op.regions[i], op.name == "builtin.func" ? &op : function, &op, i,
                      op.name == "builtin.func" ? 0 : loops + (op.name == "c.loop" && i != 0), depth + 1);
        }
    };
    visit(module.body, nullptr, nullptr, 0, 0, 0);
    return valid;
}
}
