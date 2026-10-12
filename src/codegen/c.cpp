#include <mlvm/c.hpp>
#include <mlvm/pass.hpp>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>

namespace mlvm {
namespace {
unsigned number(const Operation& op, const char* attribute) {
    auto it = op.attributes.find(attribute);
    unsigned value = 0;
    if (it == op.attributes.end()) throw std::runtime_error(std::string("missing ") + attribute);
    const auto& s = it->second;
    auto r = std::from_chars(s.data(), s.data() + s.size(), value);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) throw std::runtime_error(std::string("invalid ") + attribute);
    return value;
}
std::string symbol(const Operation& op) {
    auto it = op.attributes.find("sym");
    if (it == op.attributes.end() || it->second.empty()) throw std::runtime_error("missing symbol");
    const auto& s = it->second;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) throw std::runtime_error("invalid C symbol");
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_')) throw std::runtime_error("invalid C symbol");
    return s;
}
struct Signature { std::size_t arguments; bool is_void; };

unsigned bytes_of(std::string_view type) {
    if (type == "ptr") return 8;
    if (type.size() < 2 || type[0] != 'i') throw std::runtime_error("unsupported C value type: " + std::string(type));
    unsigned width = 0;
    const auto r = std::from_chars(type.data() + 1, type.data() + type.size(), width);
    if (r.ec != std::errc{} || r.ptr != type.data() + type.size() || (width != 8 && width != 16 && width != 32 && width != 64))
        throw std::runtime_error("unsupported C value type: " + std::string(type));
    return width / 8;
}

// Baseline frame model: every SSA value and every local owns a naturally aligned
// slot sized by its MLVM type (i8/i16/i32/i64/ptr -> 1/2/4/8). Instructions are
// emitted as encoded .byte plus a comment; only symbolic references stay text so
// the assembler can create relocations.
class FunctionEmitter {
    struct Instruction { std::vector<unsigned char> bytes; std::string text; bool symbolic = false; };
    struct Fixup { std::size_t instruction, end, label; };
    static constexpr std::array<unsigned, 6> arguments{7, 6, 2, 1, 8, 9}; // rdi rsi rdx rcx r8 r9
    const Module& module;
    const Operation& function;
    const std::map<std::string, Signature>& signatures;
    std::vector<Instruction> instructions;
    std::vector<Fixup> fixups;
    std::vector<std::size_t> labels;
    std::vector<std::pair<std::size_t, std::size_t>> loops;
    std::map<ValueId, unsigned> offsets; // byte offset from rbp
    std::map<ValueId, unsigned> sizes;
    std::vector<unsigned> local_sizes;
    std::vector<unsigned> local_offsets;
    unsigned locals;
    std::size_t offset = 0;

    unsigned size_of(ValueId value) {
        const auto* v = module.value(value);
        if (!v) throw std::runtime_error("unknown value");
        return bytes_of(module.context->spelling(v->type));
    }
    void instruction(std::span<const unsigned char> bytes, std::string text) {
        instructions.push_back({std::vector<unsigned char>(bytes.begin(), bytes.end()), std::move(text)});
        offset += bytes.size();
    }
    template <std::size_t N>
    void instruction(const unsigned char (&bytes)[N], std::string text) {
        instruction(std::span<const unsigned char>(bytes, N), std::move(text));
    }
    void imm32(std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) instructions.back().bytes.push_back(static_cast<unsigned char>(value >> (i * 8)));
        offset += 4;
    }
    void imm64(std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) instructions.back().bytes.push_back(static_cast<unsigned char>(value >> (i * 8)));
        offset += 8;
    }
    std::size_t label() { labels.push_back(SIZE_MAX); return labels.size() - 1; }
    void bind(std::size_t label) { labels.at(label) = offset; }
    void jump(std::size_t label, bool zero = false) {
        if (zero) instruction({0x0f, 0x84}, "je .L" + std::to_string(label));
        else instruction({0xe9}, "jmp .L" + std::to_string(label));
        imm32(0); fixups.push_back({instructions.size() - 1, offset, label});
    }
    static std::uint32_t disp_of(std::size_t offset_from_rbp) {
        return static_cast<std::uint32_t>(-static_cast<std::int32_t>(offset_from_rbp));
    }
    // mov <bytes> reg, [rbp-disp32]; narrow loads are zero- or sign-extended to 32 bits.
    void load_frame(unsigned slot, unsigned reg, unsigned bytes, bool sign_extend = false) {
        std::vector<unsigned char> out;
        const auto add = [&](unsigned char byte) { out.push_back(byte); };
        // REX.W widens to 64 bits; REX.R names r8-r15. 0x40 is always present.
        const auto rex_byte = 0x40U | (bytes == 8 ? 8U : 0U) | (reg >= 8 ? 4U : 0U);
        if (rex_byte != 0x40U) add(static_cast<unsigned char>(rex_byte));
        if (bytes == 1 && sign_extend) { add(0x0f); add(0xbe); }
        else if (bytes == 2 && sign_extend) { add(0x0f); add(0xbf); }
        else if (bytes == 1) { add(0x0f); add(0xb6); }
        else if (bytes == 2) { add(0x0f); add(0xb7); }
        else add(0x8b);
        add(static_cast<unsigned char>(0x85 | ((reg & 7) << 3)));
        for (unsigned i = 0; i < 4; ++i) add(static_cast<unsigned char>(disp_of(slot) >> (8 * i)));
        instruction(out, "mov frame[rbp+disp32], reg");
    }
    void store_frame(unsigned slot, unsigned reg, unsigned bytes) {
        std::vector<unsigned char> out;
        const auto add = [&](unsigned char byte) { out.push_back(byte); };
        if (bytes == 2) add(0x66);
        // REX.W widens to 64 bits; REX.R names r8-r15. 0x40 is always present.
        const auto rex_byte = 0x40U | (bytes == 8 ? 8U : 0U) | (reg >= 8 ? 4U : 0U);
        if (rex_byte != 0x40U || (bytes == 1 && reg >= 4)) add(static_cast<unsigned char>(rex_byte));
        add(static_cast<unsigned char>(bytes == 1 ? 0x88 : 0x89));
        add(static_cast<unsigned char>(0x85 | ((reg & 7) << 3)));
        for (unsigned i = 0; i < 4; ++i) add(static_cast<unsigned char>(disp_of(slot) >> (8 * i)));
        instruction(out, "mov frame[rbp+disp32], reg");
    }
    void load_value(ValueId value, unsigned reg = 0) { load_frame(offsets.at(value), reg, sizes.at(value)); }
    void save_value(ValueId value, unsigned reg = 0) { store_frame(offsets.at(value), reg, sizes.at(value)); }
    // 8/32/64-bit transfers through %rax used as an address operand. Narrow
    // loads may sign- or zero-extend into the full 32-bit register.
    void load_indirect(unsigned bytes, bool sign_extend = false) {
        std::vector<unsigned char> out;
        const auto add = [&](unsigned char byte) { out.push_back(byte); };
        if (bytes == 1) { add(0x0f); add(sign_extend ? 0xbe : 0xb6); }
        else if (bytes == 2) { add(0x0f); add(sign_extend ? 0xbf : 0xb7); }
        else {
            if (bytes == 8) add(0x48);
            add(0x8b);
        }
        add(0x00);
        instruction(out, "mov (%rax), %rax");
    }
    // Stores %rcx through the address held in %rax.
    void store_indirect(unsigned bytes) {
        std::vector<unsigned char> out;
        const auto add = [&](unsigned char byte) { out.push_back(byte); };
        if (bytes == 1) add(0x88);
        else {
            if (bytes == 2) add(0x66);
            if (bytes == 8) add(0x48);
            add(0x89);
        }
        add(0x08);
        instruction(out, "mov %ecx, (%rax)");
    }
    void frame_address(unsigned slot) {
        std::vector<unsigned char> out{0x48, 0x8d, 0x85};
        for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(disp_of(slot) >> (8 * i)));
        instruction(out, "lea frame[rbp], %rax");
    }
    // One frame layout for the whole function: every SSA value gets a naturally
    // aligned slot, then locals follow. [rbp-8, rbp) holds the saved %rbp, so
    // the cursor starts at 8 and the first slot lands at rbp-16.
    void collect(const Region& root) {
        local_sizes.assign(locals, 4);
        std::function<void(const Region&)> scan = [&](const Region& r) {
            for (const auto& block : r.blocks) for (const auto& op : block.operations) {
                if (op.name == "c.load" || op.name == "c.store")
                    local_sizes[number(op, "slot")] = std::max(local_sizes[number(op, "slot")],
                        op.name == "c.load" ? size_of(op.results[0]) : size_of(op.operands[0]));
                else if (op.name == "c.address_of" && op.attributes.at("kind") == "local")
                    local_sizes[number(op, "slot")] = std::max(local_sizes[number(op, "slot")], number(op, "bytes"));
                for (const auto& child : op.regions) scan(child);
            }
        };
        scan(root);
        unsigned cursor = 8;
        auto place = [&](ValueId value) {
            const auto bytes = size_of(value);
            cursor = (cursor + bytes - 1) & ~(bytes - 1);
            offsets[value] = cursor + bytes;
            sizes[value] = bytes;
            cursor += bytes;
        };
        std::function<void(const Region&)> allocate = [&](const Region& r) {
            for (const auto& block : r.blocks) {
                for (auto value : block.arguments) place(value);
                for (const auto& op : block.operations) {
                    for (auto value : op.results) place(value);
                    for (const auto& child : op.regions) allocate(child);
                }
            }
        };
        allocate(root);
        local_offsets.assign(locals, 0);
        for (unsigned i = 0; i < locals; ++i) {
            const auto align = 8U;
            cursor = (cursor + align - 1) & ~(align - 1);
            local_offsets[i] = cursor + local_sizes[i];
            cursor += local_sizes[i];
        }
        frame_bytes = (cursor + 15) & ~std::size_t{15};
    }
    std::size_t frame_bytes = 0;
    unsigned local_offset(const Operation& op) {
        const auto index = number(op, "slot");
        if (index >= locals) throw std::runtime_error("local slot outside function frame");
        return local_offsets[index];
    }
    void test() { instruction({0x48, 0x85, 0xc0}, "test %rax, %rax"); }
    void region(const Region& region, unsigned yield_arity) {
        const auto& block = region.blocks.at(0);
        for (const auto& op : block.operations) {
            if (op.name == "c.yield") {
                if (op.operands.size() != yield_arity) throw std::runtime_error("incorrect region yield arity");
                if (yield_arity) load_value(op.operands[0]);
            } else operation(op);
        }
    }
    void cast(const Operation& op) {
        const auto from = bytes_of(op.attributes.at("from"));
        const auto to = bytes_of(op.attributes.at("to"));
        const bool sign = op.attributes.at("signed") == "true";
        if (to <= from) return;                       // narrowing happens when the slot is stored
        if (to == 4) {                                // widen a sub-word value into 32 bits
            if (from == 1) instruction({0x0f, static_cast<unsigned char>(sign ? 0xbe : 0xb6), 0xc0}, sign ? "movsbl %al, %eax" : "movzbl %al, %eax");
            else if (from == 2) instruction({0x0f, static_cast<unsigned char>(sign ? 0xbf : 0xb7), 0xc0}, sign ? "movswl %ax, %eax" : "movzwl %ax, %eax");
            return;
        }
        const unsigned shift = 64 - from * 8;
        instruction({0x48, 0xc1, 0xe0, static_cast<unsigned char>(shift)}, "shl $" + std::to_string(shift) + ", %rax");
        if (sign) instruction({0x48, 0xc1, 0xf8, static_cast<unsigned char>(shift)}, "sar $" + std::to_string(shift) + ", %rax");
        else instruction({0x48, 0xc1, 0xe8, static_cast<unsigned char>(shift)}, "shr $" + std::to_string(shift) + ", %rax");
    }
    void binary(const std::string& n, unsigned bytes, bool is_unsigned) {
        const bool wide = bytes == 8;
        auto group = [&](std::initializer_list<unsigned char> body, const char* text) {
            std::vector<unsigned char> out;
            if (wide) out.push_back(0x48);
            out.insert(out.end(), body.begin(), body.end());
            instruction(out, text);
        };
        if (n == "arith.divsi" || n == "arith.remsi" || n == "arith.divui" || n == "arith.remui") {
            const bool unsigned_div = n == "arith.divui" || n == "arith.remui";
            if (unsigned_div) instruction({0x31, 0xd2}, "xor %edx, %edx");
            else group({0x99}, "sign extend dividend");
            group({0xf7, static_cast<unsigned char>(unsigned_div ? 0xf1 : 0xf9)}, "divide by rcx");
            if (n == "arith.remsi" || n == "arith.remui") group({0x89, 0xd0}, "mov remainder to rax");
            return;
        }
        if (n == "arith.addi") group({0x01, 0xc8}, "add %ecx, %eax");
        else if (n == "arith.subi") group({0x29, 0xc8}, "sub %ecx, %eax");
        else if (n == "arith.muli") group({0x0f, 0xaf, 0xc1}, "imul %ecx, %eax");
        else if (n == "arith.and") group({0x21, 0xc8}, "and %ecx, %eax");
        else if (n == "arith.or") group({0x09, 0xc8}, "or %ecx, %eax");
        else if (n == "arith.xor") group({0x31, 0xc8}, "xor %ecx, %eax");
        else if (n == "c.shl") group({0xd3, 0xe0}, "shl %cl, %eax");
        else if (n == "c.shr") group({0xd3, static_cast<unsigned char>(is_unsigned ? 0xe8 : 0xf8)}, "shift right by cl");
        else {
            static const std::map<std::string, unsigned char> comparisons{
                {"c.eq", 0x94}, {"c.ne", 0x95}, {"c.lt", 0x9c}, {"c.ge", 0x9d}, {"c.le", 0x9e}, {"c.gt", 0x9f}};
            auto it = comparisons.find(n);
            if (it == comparisons.end()) throw std::runtime_error("unsupported operation: " + n);
            auto condition = it->second;
            if (is_unsigned && n != "c.eq" && n != "c.ne") {
                static const std::map<std::string, unsigned char> codes{{"c.lt", 0x92}, {"c.ge", 0x93}, {"c.le", 0x96}, {"c.gt", 0x97}};
                condition = codes.at(n);
            }
            std::vector<unsigned char> out;
            if (wide) out.push_back(0x48);
            out.insert(out.end(), {0x39, 0xc8, 0x0f, condition, 0xc0, 0x0f, 0xb6, 0xc0});
            instruction(out, "cmp; setcc; movzbl %al, %eax");
        }
    }
    void operation(const Operation& op) {
        const auto& n = op.name;
        if (n == "arith.constant") {
            std::int64_t value = 0;
            const auto& text = op.attributes.at("value");
            auto r = std::from_chars(text.data(), text.data() + text.size(), value);
            if (r.ec != std::errc{}) throw std::runtime_error("malformed C constant");
            const auto bytes = sizes.at(op.results[0]);
            if (bytes == 8) { instruction({0x48, 0xb8}, "movabs $" + text + ", %rax"); imm64(static_cast<std::uint64_t>(value)); }
            else {
                if (value < INT32_MIN || value > UINT32_MAX)
                    throw std::runtime_error("C constant does not fit 32 bits");
                instruction({0xb8}, "mov $" + text + ", %eax"); imm32(static_cast<std::uint32_t>(value));
            }
            save_value(op.results[0]);
        } else if (n == "c.load") {
            load_frame(local_offset(op), 0, size_of(op.results[0]));
            save_value(op.results[0]);
        } else if (n == "c.store") {
            load_value(op.operands[0]);
            store_frame(local_offset(op), 0, size_of(op.operands[0]));
        } else if (n == "func.return") {
            if (!op.operands.empty()) load_value(op.operands[0]);
            instruction({0xc9, 0xc3}, "leave; ret");
        } else if (n == "c.call") {
            auto name = symbol(op);
            auto it = signatures.find(name);
            if (it == signatures.end() || it->second.arguments != op.operands.size() ||
                op.results.size() != (it->second.is_void ? 0U : 1U)) throw std::runtime_error("call signature mismatch: " + name);
            for (std::size_t i = op.operands.size(); i > 0; --i) load_value(op.operands[i - 1], arguments.at(i - 1));
            instructions.push_back({{0xe8, 0, 0, 0, 0}, "call " + name, true}); offset += 5;
            if (!op.results.empty()) save_value(op.results[0]);
        } else if (n == "c.if" || n == "c.select") {
            auto otherwise = label(), end = label();
            load_value(op.operands[0]); test(); jump(otherwise, true);
            region(op.regions[0], n == "c.select" ? 1 : 0);
            if (n == "c.select") save_value(op.results[0]);
            jump(end); bind(otherwise);
            region(op.regions[1], n == "c.select" ? 1 : 0);
            if (n == "c.select") save_value(op.results[0]);
            bind(end);
        } else if (n == "c.loop") {
            auto condition = label(), body = label(), step = label(), end = label();
            auto kind = op.attributes.find("kind");
            if (kind == op.attributes.end() || (kind->second != "do" && kind->second != "while")) throw std::runtime_error("invalid loop kind");
            if (kind->second == "do") jump(body);
            bind(condition);
            region(op.regions[0], 1); test(); jump(end, true);
            bind(body); loops.emplace_back(end, step);
            region(op.regions[1], 0);
            bind(step); region(op.regions[2], 0);
            loops.pop_back(); jump(condition); bind(end);
        } else if (n == "c.break" || n == "c.continue") {
            if (loops.empty()) throw std::runtime_error("loop transfer outside a loop");
            jump(n == "c.break" ? loops.back().first : loops.back().second);
        } else if (n == "c.cast") {
            load_value(op.operands[0]);
            cast(op);
            save_value(op.results[0]);
        } else if (n == "c.gep") {
            // base + index * element_size
            const auto scale = number(op, "scale");
            load_value(op.operands[0]);
            load_value(op.operands[1], 1);
            if (scale != 1) {
                std::vector<unsigned char> out{0x48, 0x69, 0xd1};
                for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(scale >> (8 * i)));
                instruction(out, "imul $" + std::to_string(scale) + ", %rcx, %rdx");
                instruction({0x48, 0x01, 0xd0}, "add %rdx, %rax");
            } else instruction({0x48, 0x01, 0xc8}, "add %rcx, %rax");
            save_value(op.results[0]);
        } else if (n == "c.ptrload") {
            load_value(op.operands[0]);
            load_indirect(size_of(op.results[0]));
            save_value(op.results[0]);
        } else if (n == "c.ptrstore") {
            load_value(op.operands[1]);            // address -> %rax
            load_value(op.operands[0], 1);          // value   -> %ecx
            store_indirect(size_of(op.operands[0]));
        } else if (n == "c.address_of") {
            if (op.attributes.at("kind") == "local") frame_address(local_offset(op));
            else { instructions.push_back({{}, "movabs $" + symbol(op) + ", %rax", true}); offset += 10; }
            save_value(op.results[0]);
        } else if (n == "arith.addi" || n == "arith.subi" || n == "arith.muli" || n == "arith.and" ||
                   n == "arith.or" || n == "arith.xor" || n == "arith.divsi" || n == "arith.remsi" || n == "arith.divui" || n == "arith.remui" ||
                   n == "c.shl" || n == "c.shr" || n == "c.lt" || n == "c.le" || n == "c.gt" ||
                   n == "c.ge" || n == "c.eq" || n == "c.ne") {
            load_value(op.operands[0]); load_value(op.operands[1], 1);
            binary(n, size_of(op.operands[0]), op.attributes.contains("unsigned") && op.attributes.at("unsigned") == "true");
            save_value(op.results[0]);
        } else throw std::runtime_error("unsupported C operation: " + n);
    }
public:
    FunctionEmitter(const Module& module, const Operation& function, const std::map<std::string, Signature>& signatures)
        : module(module), function(function), signatures(signatures),
          locals(function.attributes.contains("locals") ? number(function, "locals") : 0) {}
    std::string run() {
        const auto& entry = function.regions.at(0).blocks.at(0);
        if (entry.arguments.size() > arguments.size()) throw std::runtime_error("too many function arguments");
        collect(function.regions[0]);
        const auto name = symbol(function);
        std::ostringstream out;
        if (!function.attributes.contains("linkage") || function.attributes.at("linkage") != "internal") out << ".globl " << name << '\n';
        out << ".type " << name << ", @function\n" << name << ":\n";
        instruction({0x55, 0x48, 0x89, 0xe5}, "push %rbp; mov %rsp, %rbp");
        if (frame_bytes) { instruction({0x48, 0x81, 0xec}, "sub $" + std::to_string(frame_bytes) + ", %rsp"); imm32(static_cast<std::uint32_t>(frame_bytes)); }
        for (std::size_t i = 0; i < entry.arguments.size(); ++i) save_value(entry.arguments[i], arguments[i]);
        for (const auto& op : entry.operations) operation(op);
        for (const auto& fixup : fixups) {
            auto destination = labels.at(fixup.label);
            if (destination == SIZE_MAX) throw std::runtime_error("unbound branch target");
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(fixup.end);
            if (delta < INT32_MIN || delta > INT32_MAX) throw std::runtime_error("branch displacement overflow");
            auto& bytes = instructions.at(fixup.instruction).bytes;
            for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<unsigned char>(static_cast<std::uint32_t>(delta) >> (8 * i));
        }
        for (const auto& inst : instructions) {
            if (inst.symbolic) out << "  " << inst.text << '\n';
            else {
                out << "  # " << inst.text << '\n';
                for (unsigned byte : inst.bytes) out << "  .byte " << byte << '\n';
            }
        }
        out << ".size " << name << ", .-" << name << '\n';
        return out.str();
    }
};

void emit_global(std::ostringstream& out, const Operation& op) {
    const auto name = symbol(op);
    const auto size = number(op, "size");
    const auto align = number(op, "align");
    auto hex = op.attributes.find("init");
    const auto& bytes = hex == op.attributes.end() ? std::string{} : hex->second;
    // An empty initializer means all zeros; otherwise every byte must be present.
    if (!bytes.empty() && bytes.size() != size * 2)
        throw std::runtime_error("global initializer size mismatch: " + name);
    if (!op.attributes.contains("internal") || op.attributes.at("internal") != "true") out << ".globl " << name << '\n';
    out << ".type " << name << ", @object\n.size " << name << ", " << size
        << "\n.balign " << align << "\n" << name << ":\n";
    for (unsigned i = 0; i < size; ++i) {
        if (bytes.empty()) { out << "  .byte 0\n"; continue; }
        unsigned byte = 0;
        for (char c : bytes.substr(i * 2, 2)) {
            byte <<= 4;
            if (c >= '0' && c <= '9') byte |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') byte |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') byte |= static_cast<unsigned>(c - 'A' + 10);
            else throw std::runtime_error("malformed global initializer: " + name);
        }
        out << "  .byte " << byte << "\n";
    }
}
} // namespace

std::optional<std::string> emit_c_x86_64(const Module& input, DiagnosticEngine& diagnostics) {
    auto module = input;
    PassManager passes; passes.add(constant_fold_pass()); passes.add(dead_code_elimination_pass());
    if (!passes.run(module, diagnostics)) return std::nullopt;
    try {
        std::map<std::string, Signature> signatures;
        std::vector<const Operation*> globals;
        for (const auto& block : module.body.blocks) for (const auto& op : block.operations) {
            if (op.name == "c.global") { globals.push_back(&op); continue; }
            if (op.name != "builtin.func" && op.name != "c.declare") throw std::runtime_error("expected a C function, declaration, or global");
            auto result = op.attributes.find("result");
            const auto result_type = result == op.attributes.end() ? std::string{"void"} : result->second;
            bytes_of(result_type == "void" ? std::string_view{"ptr"} : std::string_view{result_type});
            if (result_type != "void") bytes_of(result_type);
            Signature sig{op.name == "c.declare" ? number(op, "parameters") : op.regions.at(0).blocks.at(0).arguments.size(), result_type == "void"};
            if (sig.arguments > 6) throw std::runtime_error("too many C function parameters");
            auto name = symbol(op);
            auto [it, inserted] = signatures.emplace(name, sig);
            if (!inserted && (it->second.arguments != sig.arguments || it->second.is_void != sig.is_void))
                throw std::runtime_error("incompatible function declarations: " + name);
        }
        std::ostringstream out;
        for (const char* section : {"data", "bss", "rodata"}) {
            std::vector<const Operation*> here;
            for (const auto* op : globals) if (op->attributes.at("section") == section) here.push_back(op);
            if (here.empty()) continue;
            out << "." << section << "\n";
            for (const auto* op : here) emit_global(out, *op);
        }
        out << ".text\n";
        for (const auto& block : module.body.blocks) for (const auto& op : block.operations)
            if (op.name == "builtin.func") out << FunctionEmitter(module, op, signatures).run();
        return out.str();
    } catch (const std::exception& error) {
        diagnostics.error("codegen.c", error.what()); return std::nullopt;
    }
}
}
