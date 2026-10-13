#include "test.hpp"
#include <mlvm/sched.hpp>
#include <mlvm/target.hpp>
#include <fstream>

namespace s = mlvm::sched;
namespace f = firestone::ir;
struct Fixture {
    mlvm::Module module;
    mlvm::Identifier type;
    mlvm::ValueId x, y;
    Fixture(mlvm::Context& c, unsigned bits) : module(c), type(c.intern("i" + std::to_string(bits))) {
        auto& top = module.add_block(module.body);
        auto& fn = module.append(top, "builtin.func"); fn.attributes = {{"sym", "test"}, {"result", "i" + std::to_string(bits)}};
        fn.regions.emplace_back(); const mlvm::Identifier args[]{type, type};
        auto& body = module.add_block(fn.regions[0], args); x = body.arguments[0]; y = body.arguments[1];
    }
    mlvm::Block& block() { return module.body.blocks[0].operations[0].regions[0].blocks[0]; }
    mlvm::ValueId literal(std::int64_t value) {
        auto& op = module.append(block(), "arith.constant", {}, {&type, 1}); op.attributes["value"] = std::to_string(value); return op.results[0];
    }
    mlvm::ValueId binary(std::string name, mlvm::ValueId a, mlvm::ValueId b, bool flags = false) {
        const mlvm::ValueId args[]{a, b}; auto& op = module.append(block(), "arith." + name, args, {&type, 1});
        if (flags) op.attributes["flags"] = "nsw";
        return op.results[0];
    }
    void finish(mlvm::ValueId value) { module.append(block(), "func.return", {&value, 1}); }
};

int main() { return test_main([] {
    mlvm::Context context; mlvm::DiagnosticEngine d; CHECK(mlvm::register_builtin_dialects(context, d));
    auto root = std::filesystem::path(MLVM_TEST_SOURCE_DIR) / "libstone/blackstone/kernels";
    auto kernel = s::Kernel::load(root / "expressions/AlgebraicSimplification.bst", {}, d); CHECK(kernel);
    auto proven = [](mlvm::ValueId, std::string_view) { return BS_TRUE; };
    const char* names[]{"AddZero", "AddZeroRight", "SubtractZero", "SubtractSelf", "MultiplyOne", "MultiplyOneRight",
        "MultiplyZero", "MultiplyZeroRight", "DivideOne", "AndZero", "AndSelf", "OrZero", "XorSelf", "AddConstantRight",
        "ReassociateAddConstants", "ReassociateMultiplyConstants"};
    for (unsigned rule = 0; rule < 16; ++rule) {
        Fixture fixture(context, 8); auto zero = fixture.literal(0), one = fixture.literal(1), three = fixture.literal(3), four = fixture.literal(4);
        auto x = fixture.x; mlvm::ValueId result = 0;
        switch (rule) {
        case 0: result = fixture.binary("addi", zero, x); break;
        case 1: result = fixture.binary("addi", x, zero); break;
        case 2: result = fixture.binary("subi", x, zero); break;
        case 3: result = fixture.binary("subi", x, x); break;
        case 4: result = fixture.binary("muli", one, x); break;
        case 5: result = fixture.binary("muli", x, one); break;
        case 6: result = fixture.binary("muli", zero, x); break;
        case 7: result = fixture.binary("muli", x, zero); break;
        case 8: result = fixture.binary("divsi", x, one); break;
        case 9: result = fixture.binary("and", x, zero); break;
        case 10: result = fixture.binary("and", x, x); break;
        case 11: result = fixture.binary("or", x, zero); break;
        case 12: result = fixture.binary("xor", x, x); break;
        case 13: result = fixture.binary("addi", three, x); break;
        case 14: result = fixture.binary("addi", fixture.binary("addi", x, three), four); break;
        case 15: result = fixture.binary("muli", fixture.binary("muli", x, three), four); break;
        }
        fixture.finish(result); std::vector<s::Event> trace; auto before = fixture.module;
        CHECK(s::apply(*kernel, fixture.module, 100, trace, d, proven));
        CHECK(std::any_of(trace.begin(), trace.end(), [&](const auto& event) { return event.action == names[rule]; }));
        for (std::uint64_t value = 0; value < 256; ++value) {
            const std::uint64_t args[]{value, 9};
            CHECK(mlvm::interpret(before, args, d) == mlvm::interpret(fixture.module, args, d));
        }
        CHECK(mlvm::verify(fixture.module, d));
    }
    // Exhaustive signed widths 1..4 against independent modular arithmetic.
    const char* ops[]{"addi", "subi", "muli", "divsi", "and", "or", "xor"};
    for (unsigned bits = 1; bits <= 4; ++bits) {
        const int modulus = 1 << bits, sign = modulus / 2;
        for (unsigned op = 0; op < 7; ++op) for (int a = 0; a < modulus; ++a) for (int b = 0; b < modulus; ++b) {
            const int av = a >= sign ? a - modulus : a, bv = b >= sign ? b - modulus : b;
            if (op == 3 && (bv == 0 || (av == -sign && bv == -1))) continue;
            int expected = op == 0 ? av + bv : op == 1 ? av - bv : op == 2 ? av * bv : op == 3 ? av / bv : op == 4 ? a & b : op == 5 ? a | b : a ^ b;
            expected = (expected % modulus + modulus) % modulus;
            Fixture fixture(context, bits); auto left = fixture.literal(av), right = fixture.literal(bv);
            fixture.finish(fixture.binary(ops[op], left, right)); std::vector<s::Event> events;
            CHECK(s::apply(*kernel, fixture.module, 100, events, d));
            const std::uint64_t args[]{0, 0}; auto value = mlvm::interpret(fixture.module, args, d);
            CHECK(value && *value == static_cast<std::uint64_t>(expected)); CHECK(!events.empty());
        }
    }
    // Unknown proofs, semantic flags, identity-distinct inputs, and signed i1 exact(1).
    for (unsigned test = 0; test < 5; ++test) {
        Fixture fixture(context, test == 4 ? 1 : 8); auto zero = fixture.literal(0); mlvm::ValueId result;
        if (test == 0) result = fixture.binary("muli", zero, fixture.x);
        else if (test == 1) result = fixture.binary("subi", fixture.x, fixture.y);
        else if (test == 2) result = fixture.binary("addi", fixture.x, zero, true);
        else if (test == 3) result = fixture.binary("divsi", fixture.literal(-128), fixture.literal(-1));
        else result = fixture.binary("muli", fixture.x, fixture.literal(-1));
        fixture.finish(result); auto text = mlvm::print_ir(fixture.module); std::vector<s::Event> events;
        CHECK(s::apply(*kernel, fixture.module, 100, events, d)); CHECK(events.empty()); CHECK(mlvm::print_ir(fixture.module) == text);
    }
    // Unsigned Firestone folding uses that artifact's explicit interpretation.
    for (unsigned bits = 2; bits <= 4; ++bits) {
        const unsigned modulus = 1u << bits;
        const f::Opcode fire_ops[]{f::Opcode::Add, f::Opcode::Sub, f::Opcode::Mul, f::Opcode::Div, f::Opcode::And, f::Opcode::Or, f::Opcode::Xor};
        for (unsigned op = 0; op < 7; ++op) for (unsigned a = 0; a < modulus; ++a) for (unsigned b = 0; b < modulus; ++b) {
            if (op == 3 && !b) continue;
            auto expected = op == 0 ? a + b : op == 1 ? a - b : op == 2 ? a * b : op == 3 ? a / b : op == 4 ? a & b : op == 5 ? a | b : a ^ b;
            s::Dataflow flow; flow.is_signed = false; auto type = f::Type::integer(bits);
            auto left = flow.graph.constant(a, type), right = flow.graph.constant(b, type);
            flow.roots = {flow.graph.operation(fire_ops[op], type, {left, right})}; std::vector<s::Event> events;
            CHECK(s::apply(*kernel, flow, 100, events, d));
            auto result = flow.graph.node(flow.roots[0]); CHECK(result.opcode == f::Opcode::Constant);
            CHECK(result.operands[0].integer == static_cast<std::int64_t>(expected & (modulus - 1)));
        }
    }
    CHECK(context.register_dialect("host", d));
    CHECK(context.register_operation({"host.effect", 0, 0, 1, 1, 0, 0, mlvm::Effect::unknown, false, {}}, d));
    for (bool retain : {false, true}) {
        Fixture fixture(context, 8); auto zero = fixture.literal(0);
        auto effect = fixture.module.append(fixture.block(), "host.effect", {}, {&fixture.type, 1}).results[0];
        fixture.finish(fixture.binary(retain ? "addi" : "muli", effect, zero));
        std::vector<s::Event> events; CHECK(s::apply(*kernel, fixture.module, 100, events, d));
        CHECK(std::count_if(fixture.block().operations.begin(), fixture.block().operations.end(), [](const auto& op) { return op.name == "host.effect"; }) == 1);
        CHECK(retain ? !events.empty() : events.empty());
        if (retain) CHECK(fixture.block().operations.back().operands[0] == effect);
    }
    // The complete supplied catalog resolves through the actual native parser/compiler.
    const std::filesystem::path directories[]{root}; std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) if (entry.path().extension() == ".bst") {
        auto loaded = s::Kernel::load(entry.path(), directories, d); CHECK(loaded); ++count;
    }
    CHECK(count >= 85);
    auto temporary = std::filesystem::path(MLVM_TEST_BINARY_DIR) / "invalid.bst";
    for (const auto* source : {
        "Kernel Bad; /* open",
        "Kernel Bad; concerns Unknown::Term;",
        "Kernel Bad(inherits Missing);",
        "Kernel Bad; concerns HIR::Literals::Term; R :: Term!same($1) -> Term where Pure($1) { $1 ~> $1; }",
        "Kernel Bad; concerns HIR::Literals::Term; R :: Term!resolves -> Term where Unknown { $0 ~> $1; }",
        "Kernel Bad; concerns HIR::Literals::Term; R :: Term!resolves(1) -> Term where Unknown { $1 ~> $1; }"}) {
        { std::ofstream file(temporary); file << source; }
        d.clear(); CHECK(!s::Kernel::load(temporary, {}, d)); CHECK(d.has_error());
    }
    d.clear(); CHECK(!d.has_error());
}); }
