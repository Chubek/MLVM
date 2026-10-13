#include <mlvm/target.hpp>
#include <bit>
#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>

namespace mlvm {
namespace {
struct Function { const Operation* op; const Block* block; };
std::vector<Function> functions(const Module& m) {
    std::vector<Function> result;
    for (const auto& block : m.body.blocks) for (const auto& op : block.operations) {
        if (op.name != "builtin.func" || op.regions.size() != 1 || op.regions[0].blocks.size() != 1)
            throw std::runtime_error("target requires single-block builtin.func definitions");
        const auto& sym = op.attributes.at("sym");
        if (sym.empty() || !(std::isalpha(static_cast<unsigned char>(sym[0])) || sym[0] == '_')) throw std::runtime_error("target requires an identifier symbol");
        for (unsigned char c : sym) if (!(std::isalnum(c) || c == '_')) throw std::runtime_error("invalid target symbol");
        result.push_back({&op, &op.regions[0].blocks[0]});
    }
    if (result.empty()) throw std::runtime_error("target requires at least one function");
    return result;
}
unsigned width(const Module& m, ValueId id) {
    auto* value = m.value(id); if (!value) throw std::runtime_error("unknown target value");
    auto s = m.context->spelling(value->type); unsigned bits = 0;
    if (s.size() < 2 || s[0] != 'i') throw std::runtime_error("target requires integer values");
    auto r = std::from_chars(s.data() + 1, s.data() + s.size(), bits);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !bits || bits > 64) throw std::runtime_error("unsupported target integer type");
    return bits;
}
std::uint64_t mask(unsigned bits) { return bits == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << bits) - 1; }
std::int64_t constant(const Operation& op) {
    auto s = op.attributes.at("value"); std::int64_t value = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), value);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) throw std::runtime_error("invalid target constant");
    return value;
}
std::string opcode(const Operation& op) {
    if (op.name == "func.return") return "return";
    auto prefix = op.name.substr(0, op.name.find('.'));
    if (prefix != "arith" && prefix != "high" && prefix != "mid" && prefix != "low" && prefix != "machine" &&
        prefix != "interp" && prefix != "wasm" && prefix != "llvmir" && prefix != "gpu") throw std::runtime_error("unsupported target operation: " + op.name);
    for (const auto& [key, value] : op.attributes) {
        (void)value;
        if (key != "value" && key != "origin") throw std::runtime_error("target cannot discard semantic attribute '" + key + "'");
    }
    if (!op.regions.empty()) throw std::runtime_error("target does not support nested control flow");
    auto name = op.name.substr(op.name.find('.') + 1);
    if (name == "addi") name = "add"; else if (name == "subi") name = "sub";
    else if (name == "muli") name = "mul"; else if (name == "divsi") name = "div";
    if (name != "constant" && name != "add" && name != "sub" && name != "mul" && name != "div" &&
        name != "and" && name != "or" && name != "xor" && name != "select" && name != "copy") throw std::runtime_error("unsupported target operation: " + op.name);
    return name;
}
std::uint64_t quotient(std::uint64_t a, std::uint64_t b, unsigned bits) {
    auto sign = std::uint64_t{1} << (bits - 1), all = mask(bits);
    if (!b || (a == sign && b == all)) throw std::runtime_error("integer division traps");
    bool an = a & sign, bn = b & sign;
    auto am = an ? (~a & all) + 1 : a, bm = bn ? (~b & all) + 1 : b;
    auto value = am / bm; return (an != bn ? std::uint64_t{0} - value : value) & all;
}
using Bytes = std::vector<std::uint8_t>;
void uleb(Bytes& out, std::uint64_t value) {
    do { auto byte = static_cast<std::uint8_t>(value & 127); value >>= 7; out.push_back(byte | (value ? 128 : 0)); } while (value);
}
void sleb(Bytes& out, std::int64_t value) {
    bool more;
    do { auto byte = static_cast<std::uint8_t>(value & 127); value >>= 7;
        more = !((value == 0 && !(byte & 64)) || (value == -1 && (byte & 64)));
        out.push_back(byte | (more ? 128 : 0));
    } while (more);
}
void string(Bytes& out, std::string_view s) { uleb(out, s.size()); out.insert(out.end(), s.begin(), s.end()); }
void section(Bytes& out, std::uint8_t id, const Bytes& content) {
    out.push_back(id); uleb(out, content.size()); out.insert(out.end(), content.begin(), content.end());
}
}
std::optional<std::uint64_t> interpret(const Module& m, std::span<const std::uint64_t> args, DiagnosticEngine& d) {
    if (!verify(m, d)) return {};
    try {
        auto list = functions(m); if (list.size() != 1) throw std::runtime_error("interpreter requires exactly one function");
        const auto& block = *list[0].block;
        if (args.size() != block.arguments.size()) throw std::runtime_error("interpreter argument count mismatch");
        std::map<ValueId, std::uint64_t> values;
        for (std::size_t i = 0; i < args.size(); ++i) values[block.arguments[i]] = args[i] & mask(width(m, block.arguments[i]));
        for (const auto& op : block.operations) {
            auto name = opcode(op);
            if (name == "return") return op.operands.empty() ? 0 : values.at(op.operands[0]);
            auto id = op.results.at(0); auto bits = width(m, id); std::uint64_t value = 0;
            if (name == "constant") value = static_cast<std::uint64_t>(constant(op));
            else {
                auto a = values.at(op.operands.at(0)); auto b = op.operands.size() > 1 ? values.at(op.operands[1]) : 0;
                if (name == "add") value = a + b; else if (name == "sub") value = a - b;
                else if (name == "mul") value = a * b; else if (name == "div") value = quotient(a, b, bits);
                else if (name == "and") value = a & b; else if (name == "or") value = a | b;
                else if (name == "xor") value = a ^ b; else if (name == "copy") value = a;
                else if (name == "select") value = a ? b : values.at(op.operands[2]);
            }
            values[id] = value & mask(bits);
        }
        throw std::runtime_error("interpreter function has no return");
    } catch (const std::exception& e) { d.error("target.interp", e.what()); return {}; }
}
std::optional<std::string> emit_llvm_ir(const Module& m, DiagnosticEngine& d) {
    if (!verify(m, d)) return {};
    try {
        std::ostringstream out; out << "; MLVM integer SSA; target layout supplied by the LLVM consumer\n";
        for (auto [function, block] : functions(m)) {
            auto result = function->attributes.contains("result") ? function->attributes.at("result") : "void";
            out << "define " << result << " @" << function->attributes.at("sym") << '(';
            auto type = [&](ValueId id) { return "i" + std::to_string(width(m, id)); };
            for (std::size_t i = 0; i < block->arguments.size(); ++i) { if (i) out << ", "; out << type(block->arguments[i]) << " %v" << block->arguments[i]; }
            out << ") {\nentry:\n";
            for (const auto& op : block->operations) {
                auto name = opcode(op);
                if (name == "return") { out << "  ret "; if (op.operands.empty()) out << "void"; else out << type(op.operands[0]) << " %v" << op.operands[0]; out << '\n'; continue; }
                auto id = op.results.at(0); auto t = type(id);
                const bool wide_condition = name == "select" && width(m, op.operands[0]) != 1;
                if (wide_condition) out << "  %cond" << id << " = icmp ne " << type(op.operands[0]) << " %v" << op.operands[0] << ", 0\n";
                out << "  %v" << id << " = ";
                if (name == "constant") out << "add " << t << " 0, " << constant(op);
                else if (name == "copy") out << "add " << t << " 0, %v" << op.operands[0];
                else if (name == "select") out << "select i1 %" << (wide_condition ? "cond" : "v") << (wide_condition ? id : op.operands[0]) << ", " << t << " %v" << op.operands[1] << ", " << t << " %v" << op.operands[2];
                else out << (name == "div" ? "sdiv" : name) << ' ' << t << " %v" << op.operands[0] << ", %v" << op.operands[1];
                out << '\n';
            }
            out << "}\n";
        }
        return out.str();
    } catch (const std::exception& e) { d.error("target.llvmir", e.what()); return {}; }
}
std::optional<std::vector<std::uint8_t>> emit_wasm(const Module& m, DiagnosticEngine& d) {
    if (!verify(m, d)) return {};
    try {
        auto list = functions(m); Bytes types, funcs, exports, code;
        uleb(types, list.size()); uleb(funcs, list.size()); uleb(exports, list.size()); uleb(code, list.size());
        auto wasm_type = [&](ValueId id) -> std::uint8_t {
            auto bits = width(m, id); if (bits != 32 && bits != 64) throw std::runtime_error("WebAssembly requires i32 or i64"); return bits == 32 ? 0x7f : 0x7e;
        };
        for (std::size_t i = 0; i < list.size(); ++i) {
            auto [function, block] = list[i]; const auto& ret = block->operations.back();
            if (ret.name != "func.return") throw std::runtime_error("WebAssembly function must return");
            types.push_back(0x60); uleb(types, block->arguments.size());
            for (auto id : block->arguments) types.push_back(wasm_type(id));
            uleb(types, ret.operands.size()); if (!ret.operands.empty()) types.push_back(wasm_type(ret.operands[0]));
            uleb(funcs, i); string(exports, function->attributes.at("sym")); exports.push_back(0); uleb(exports, i);
            Bytes body; std::map<ValueId, std::size_t> locals; std::vector<ValueId> results;
            for (auto id : block->arguments) locals[id] = locals.size();
            for (const auto& op : block->operations) for (auto id : op.results) { locals[id] = locals.size(); results.push_back(id); }
            uleb(body, results.size()); for (auto id : results) { uleb(body, 1); body.push_back(wasm_type(id)); }
            auto get = [&](ValueId id) { body.push_back(0x20); uleb(body, locals.at(id)); };
            for (const auto& op : block->operations) {
                auto name = opcode(op);
                if (name == "return") { for (auto id : op.operands) get(id); body.push_back(0x0f); continue; }
                auto id = op.results.at(0); bool wide = wasm_type(id) == 0x7e;
                if (name == "constant") {
                    body.push_back(wide ? 0x42 : 0x41); auto value = constant(op);
                    if (!wide) value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
                    sleb(body, value);
                } else if (name == "select") { get(op.operands[1]); get(op.operands[2]); get(op.operands[0]); body.push_back(0x1b); }
                else {
                    for (auto operand : op.operands) get(operand);
                    if (name != "copy") {
                        static const std::map<std::string, std::uint8_t> codes{{"add", 0x6a}, {"sub", 0x6b}, {"mul", 0x6c}, {"div", 0x6d}, {"and", 0x71}, {"or", 0x72}, {"xor", 0x73}};
                        body.push_back(codes.at(name) + (wide ? 0x12 : 0));
                    }
                }
                body.push_back(0x21); uleb(body, locals.at(id));
            }
            body.push_back(0x0b); uleb(code, body.size()); code.insert(code.end(), body.begin(), body.end());
        }
        Bytes out{0, 0x61, 0x73, 0x6d, 1, 0, 0, 0}; section(out, 1, types); section(out, 3, funcs); section(out, 7, exports); section(out, 10, code); return out;
    } catch (const std::exception& e) { d.error("target.wasm", e.what()); return {}; }
}
std::optional<Object> emit_x86_object(const Module& m, DiagnosticEngine& d) {
    if (!verify(m, d)) return {};
    try {
        Object object; object.sections.push_back({".text", 6, 16, {}}); auto& bytes = object.sections[0].data;
        auto emit = [&](std::initializer_list<std::uint8_t> data) { bytes.insert(bytes.end(), data); };
        auto imm = [&](std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i))); };
        for (auto [function, block] : functions(m)) {
            if (block->arguments.size() > 6) throw std::runtime_error("x86 expression target accepts at most six i32 arguments");
            // Report selection explicitly: every select condition is narrower than the
            // i32-only frame, so width allocation would otherwise misdiagnose it.
            for (const auto& op : block->operations)
                if (opcode(op) == "select") throw std::runtime_error("x86 select requires condition lowering");
            auto start = bytes.size(); std::map<ValueId, std::uint32_t> slots;
            auto allocate = [&](ValueId id) {
                if (width(m, id) != 32) throw std::runtime_error("x86 expression target requires i32 values");
                if (slots.size() >= (std::numeric_limits<std::int32_t>::max() - 16) / 4) throw std::runtime_error("x86 frame is too large");
                slots[id] = static_cast<std::uint32_t>((slots.size() + 1) * 4);
            };
            for (auto id : block->arguments) allocate(id);
            for (const auto& op : block->operations) for (auto id : op.results) allocate(id);
            emit({0x55, 0x48, 0x89, 0xe5, 0x48, 0x81, 0xec}); imm(static_cast<std::uint32_t>((slots.size() * 4 + 15) & ~std::size_t{15}));
            const unsigned registers[]{7, 6, 2, 1, 8, 9};
            auto store = [&](ValueId id, unsigned reg) { if (reg >= 8) emit({0x44}); emit({0x89, static_cast<std::uint8_t>(0x85 | ((reg & 7) << 3))}); imm(0u - slots.at(id)); };
            auto load = [&](ValueId id, unsigned reg) { emit({0x8b, static_cast<std::uint8_t>(0x85 | (reg << 3))}); imm(0u - slots.at(id)); };
            for (std::size_t i = 0; i < block->arguments.size(); ++i) store(block->arguments[i], registers[i]);
            for (const auto& op : block->operations) {
                auto name = opcode(op);
                if (name == "return") { if (!op.operands.empty()) load(op.operands[0], 0); emit({0xc9, 0xc3}); continue; }
                if (name == "constant") { emit({0xb8}); imm(static_cast<std::uint32_t>(constant(op))); }
                else {
                    load(op.operands[0], 0);
                    if (name != "copy") load(op.operands[1], 1);
                    if (name == "add") emit({0x01, 0xc8}); else if (name == "sub") emit({0x29, 0xc8});
                    else if (name == "mul") emit({0x0f, 0xaf, 0xc1}); else if (name == "div") emit({0x99, 0xf7, 0xf9});
                    else if (name == "and") emit({0x21, 0xc8}); else if (name == "or") emit({0x09, 0xc8});
                    else if (name == "xor") emit({0x31, 0xc8}); else if (name == "select") throw std::runtime_error("x86 select requires condition lowering");
                }
                store(op.results.at(0), 0);
            }
            if (block->operations.back().name != "func.return") throw std::runtime_error("x86 function must return");
            object.symbols.push_back({function->attributes.at("sym"), Binding::global, 2, 0, start, bytes.size() - start});
        }
        return object;
    } catch (const std::exception& e) { d.error("target.x86_64", e.what()); return {}; }
}
} // namespace mlvm
