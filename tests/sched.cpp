#include "test.hpp"
#include <mlvm/sched.hpp>
#include <mlvm/dialect.hpp>
#include <mlvm/target.hpp>
#include <fstream>

namespace s = mlvm::sched;
namespace f = firestone::ir;
static const std::filesystem::path root = MLVM_TEST_SOURCE_DIR;

int main() { return test_main([] {
    mlvm::Context context; mlvm::DiagnosticEngine d;
    CHECK(mlvm::register_builtin_dialects(context, d));
    auto registry = s::Registry::builtins(context);
    auto graph = s::load(root / "examples/sched/algebra.sched", registry, d);
    CHECK(graph); CHECK(graph->order(d) == std::vector<std::string>({"input", "simplify", "clean", "answer"}));
    s::Result result;
    CHECK(s::Scheduler{}.run(*graph, result, d, root / "examples/sched"));
    CHECK(result.artifacts.at("answer").get<std::string>() == "42\n");
    CHECK(mlvm::print_ir(result.artifacts.at("input").get<mlvm::Module>()).find("\"value\"=\"3\"") != std::string::npos);
    CHECK(std::any_of(result.events.begin(), result.events.end(), [](const auto& e) { return e.action == "ReassociateAddConstants"; }));
    CHECK(std::any_of(result.events.begin(), result.events.end(), [](const auto& e) { return e.action == "ConstantFolding"; }));

    auto firestone = s::load(root / "examples/sched/firestone.sched", registry, d); CHECK(firestone);
    CHECK(s::Scheduler{}.run(*firestone, result, d, root / "examples/sched"));
    CHECK(result.artifacts.at("linear").get<f::Typed3AC>().verify().empty());
    CHECK(result.artifacts.at("ssa").get<f::SimpleSSAGraph>().verify().empty());
    const auto& flow = result.artifacts.at("simplified").get<s::Dataflow>();
    const auto& final = flow.graph.node(flow.roots[0]); CHECK(final.opcode == f::Opcode::Add);
    CHECK(flow.graph.node(final.operands[1].value).operands[0].integer == 7);

    for (const auto* source : {
        "lsched.node{name='a',action='verify',inputs={'missing'}}",
        "lsched.node{name='a',action='verify',inputs={'a'}}",
        "lsched.node{name='a',action='verify',inputs={'b'}}; lsched.node{name='b',action='verify',inputs={'a'}}",
        "lsched.node{name='a',action='verify'}; lsched.node{name='a',action='verify'}",
        "lsched.node{name='a',action='missing'}",
        "lsched.node{name='a',action='verify',inputs={[2]='b'}}",
        "lsched.node{name='a',action='verify',options={typo='x'}}",
        "lsched.node{name='a',action='kernel',options={path='x',budget='-1'}}",
        "local x = )",
        "error('deliberate')"}) {
        d.clear(); CHECK(!s::parse(source, "bad.sched", registry, d)); CHECK(d.has_error());
    }
    d.clear();
    auto forward = s::load(root / "examples/sched/dependencies.sched", registry, d); CHECK(forward);
    CHECK(forward->order(d) == std::vector<std::string>({"input", "check", "optimized", "finish"}));

    // A failing node must leave the caller's previous result untouched.
    s::Graph failure;
    CHECK(failure.add({"first", "host", {}, {}, [](auto, auto&) { return s::Artifact{"text", std::string("new")}; }}, d));
    CHECK(failure.add({"broken", "host", {"first"}, {}, [](auto, auto&) -> std::optional<s::Artifact> { throw std::runtime_error("failed"); }}, d));
    auto old_size = result.artifacts.size();
    CHECK(!s::Scheduler{}.run(failure, result, d)); CHECK(result.artifacts.size() == old_size); d.clear();
    auto kernel = s::Kernel::load(root / "libstone/blackstone/kernels/expressions/AlgebraicSimplification.bst", {}, d);
    CHECK(kernel); CHECK(kernel->rules().size() == 17);
    auto original = s::Registry::builtins(context).make("ir", {{"path", (root / "examples/sched/input.mlvm").string()}});
    std::vector<s::Event> events; std::set<std::filesystem::path> files; s::RunContext run{d, events, ".", files};
    auto input = original({}, run); CHECK(input); auto module = input->get<mlvm::Module>();
    const auto before = mlvm::print_ir(module);
    CHECK(!s::apply(*kernel, module, 0, events, d)); CHECK(mlvm::print_ir(module) == before); CHECK(events.empty()); d.clear();
    CHECK(s::apply(*kernel, module, 1000, events, d));
    auto once = mlvm::print_ir(module); auto count = events.size();
    CHECK(s::apply(*kernel, module, 1000, events, d)); CHECK(mlvm::print_ir(module) == once); CHECK(events.size() == count);
    auto profile = std::filesystem::path(MLVM_TEST_BINARY_DIR) / "algebra.bsp";
    CHECK(kernel->write_profile(profile, d)); auto loaded = s::Kernel::read_profile(profile, d); CHECK(loaded); CHECK(loaded->rules() == kernel->rules());

    // Every dialect is registered and every expression conversion preserves behavior.
    const std::uint64_t arg = 35;
    for (const auto* dialect : {"high", "mid", "low", "machine", "interp", "llvmir", "wasm", "gpu"}) {
        CHECK(context.has_dialect(dialect)); auto lowered = module;
        CHECK(mlvm::lower_dialect(lowered, dialect, d));
        auto value = mlvm::interpret(lowered, {&arg, 1}, d); CHECK(value && *value == 42);
    }
    CHECK(mlvm::emit_wasm(module, d)); CHECK(mlvm::emit_llvm_ir(module, d));
    auto object = mlvm::emit_x86_object(module, d); CHECK(object); CHECK(object->symbols[0].name == "pipeline");
    auto bytes = mlvm::write_elf(*object, d); CHECK(bytes); CHECK(mlvm::read_elf(*bytes, d));
    CHECK(mlvm::TargetRegistry::builtins().list().size() == 4);
    // A legal-looking conversion that loses a width/semantic contract must roll back.
    auto narrow = input->get<mlvm::Module>();
    auto narrow_type = context.intern("i8");
    for (auto& [id, value] : narrow.values) { (void)id; value.type = narrow_type; }
    narrow.body.blocks[0].operations[0].attributes["result"] = "i8";
    auto narrow_text = mlvm::print_ir(narrow);
    CHECK(!mlvm::lower_dialect(narrow, "wasm", d)); CHECK(mlvm::print_ir(narrow) == narrow_text); d.clear();
    mlvm::SourceManager sources;
    for (const auto* bad : {
        "mlvm 1 { block(%x:\"i32\") { %r:\"i64\" = \"machine.copy\"(%x) []; } }",
        "mlvm 1 { block(%x:\"i32\") { %r:\"i8\" = \"low.cast\"(%x) [\"kind\"=\"sext\"]; } }",
        "mlvm 1 { block(%p:\"ptr\") { %r:\"i32\" = \"low.load\"(%p) [\"alignment\"=\"3\"]; } }",
        "mlvm 1 { block(%x:\"i32\") { \"interp.guard\"(%x) [\"deopt\"=\"slow\"]; } }",
        "mlvm 1 { block() { %r:\"i32\" = \"gpu.thread_id\"() [\"axis\"=\"w\"]; } }",
        "mlvm 1 { block() { %r:\"index\" = \"index.constant\"() [\"value\"=\"-1\"]; } }",
        "mlvm 1 { block(%c:\"i1\") { %r:\"i32\" = \"high.if\"(%c) [] region { block() { \"high.yield\"() []; } } region { block() { \"high.yield\"() []; } }; } }"}) {
        CHECK(!mlvm::parse_ir(context, sources, sources.add("invalid", bad), d)); CHECK(d.has_error()); d.clear();
    }
    CHECK(!d.has_error());
}); }
