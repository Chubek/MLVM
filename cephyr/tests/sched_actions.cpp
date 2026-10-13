#include "../../tests/test.hpp"
#include <cephyr/sched.hpp>
#include <cephyr/frontend.hpp>
#include <mlvm/target.hpp>

namespace s = mlvm::sched;

static bool throws_invalid(const s::Registry& registry, const s::Options& options) {
    try {
        registry.make("cephyr", options);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

static std::optional<s::Graph> pipeline(const s::Registry& registry, std::string_view body, mlvm::DiagnosticEngine& d) {
    std::string source = "local p = require(\"lsched\").pipeline()\n";
    source += body;
    source += "return p\n";
    return s::parse(source, "cephyr.sched", registry, d);
}

int main() { return test_main([] {
    mlvm::Context context; mlvm::DiagnosticEngine d;
    CHECK(mlvm::register_builtin_dialects(context, d));
    s::Registry registry = s::Registry::builtins(context);
    CHECK(cephyr::register_scheduler_actions(registry, context));

    // Option validation happens at load time, before any action runs.
    CHECK(throws_invalid(registry, {}));
    CHECK(throws_invalid(registry, {{"path", "a.c"}, {"source", "int x;"}}));
    CHECK(throws_invalid(registry, {{"source", "int x;"}, {"preprocessor", "bogus"}}));
    CHECK(throws_invalid(registry, {{"source", "int x;"}, {"typo", "x"}}));
    CHECK(throws_invalid(registry, {{"source", "int x;"}, {"defines", "9bad"}}));
    CHECK(throws_invalid(registry, {{"source", "int x;"}, {"defines", "A,,B"}}));
    CHECK(throws_invalid(registry, {{"source", "int x;"}, {"includes", "inc,,other"}}));
    // The `source` form needs no files; the working directory is unused.
    // A failed run leaves the caller's previous result untouched.
    s::Result result;
    auto run = [&](std::string_view body) {
        auto graph = pipeline(registry, body, d);
        CHECK(graph);
        return s::Scheduler{}.run(*graph, result, d, ".");
    };

    // C source lowers to a verified C-dialect module; the x86-64 target route
    // emits it through the C backend to an ELF relocatable object.
    CHECK(run("local src = p:cephyr(\"src\", {source=\"int answer(void) { int x = 40; return x + 2; }\\n\"})\n"
              "local obj = p:target(\"obj\", src, {triple=\"x86_64-unknown-linux-gnu\"})\n"
              "p:text(\"ir\", src)\n"));
    CHECK(mlvm::print_ir(result.artifacts.at("src").get<mlvm::Module>()).find("builtin.func") != std::string::npos);
    CHECK(mlvm::print_ir(result.artifacts.at("src").get<mlvm::Module>()).find("answer") != std::string::npos);
    CHECK(mlvm::read_elf(result.artifacts.at("obj").get<std::vector<std::uint8_t>>(), d));
    CHECK(result.artifacts.at("ir").get<std::string>().find("c.") != std::string::npos);
    d.clear();

    // Preprocessor options flow through: the macro must be defined for lowering.
    CHECK(run("local src = p:cephyr(\"src\", {source=\"#if VAL == 42\\nint answer(void) { return VAL; }\\n"
              "#else\\n#error nope\\n#endif\\n\", defines=\"VAL=42\"})\n"));
    d.clear();
    CHECK(!run("local src = p:cephyr(\"src\", {source=\"#if VAL == 42\\nint answer(void) { return VAL; }\\n"
               "#else\\n#error nope\\n#endif\\n\"})\n"));
    d.clear();

    // Semantic failures fail the node without publishing.
    {
        auto size = result.artifacts.size();
        CHECK(!run("local src = p:cephyr(\"src\", {source=\"int f(void) { return missing; }\\n\"})\n"));
        CHECK(result.artifacts.size() == size);
        d.clear();
    }

    // Modules using C memory operations have no expression lowering or
    // non-x86 emission; conversions and foreign targets fail explicitly.
    // (Pure-expression C functions lower to arith and flow through the
    // standard pipeline unchanged.)
    CHECK(!run("local src = p:cephyr(\"src\", {source=\"int answer(void) { int x = 40; return x + 2; }\\n\"})\n"
               "p:convert(\"low\", src, {to=\"low\"})\n"));
    d.clear();
    CHECK(!run("local src = p:cephyr(\"src\", {source=\"int answer(void) { int x = 40; return x + 2; }\\n\"})\n"
               "p:target(\"wasm\", src, {triple=\"wasm32-unknown-unknown\"})\n"));
    d.clear();
    CHECK(!d.has_error());
}); }
