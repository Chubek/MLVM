#include "../../tests/test.hpp"
#include <cephyr/frontend.hpp>

int main() { return test_main([] {
    mlvm::SourceManager sources; mlvm::DiagnosticEngine diagnostics; mlvm::Context context;
    auto preprocess = [&](std::string code, cephyr::Preprocessor engine = cephyr::Preprocessor::Ucpp) {
        cephyr::PreprocessOptions options; options.engine = engine;
        return cephyr::preprocess(sources, sources.add("test.c", std::move(code)), options, diagnostics);
    };
    // The default engine is ucpp: full C99 preprocessing.
    auto expanded = preprocess("#define A 6\n#define B (A * 7)\n#ifdef A\nint answer(void) { return B; }\n#else\ninvalid\n#endif\n");
    CHECK(expanded); CHECK(expanded->find("invalid") == std::string::npos);
    { // ucpp normalizes inter-token whitespace while expanding.
        const auto six = expanded->find('6'), star = expanded->find('*'), seven = expanded->find('7');
        CHECK(six != std::string::npos && star != std::string::npos && seven != std::string::npos);
        CHECK(six < star && star < seven);
    }
    expanded = preprocess("#define F(x) ((x) + 1)\n#define G 6\nint answer(void) { return F(G) * 7; }\n");
    CHECK(expanded); CHECK(expanded->find("6") != std::string::npos); CHECK(expanded->find("F(G)") == std::string::npos);
    expanded = preprocess("#define STR(x) #x\nconst char* s = STR(hi);\n");
    CHECK(expanded); CHECK(expanded->find("\"hi\"") != std::string::npos);
    expanded = preprocess("#define CAT(a, b) a ## b\nint n = CAT(4, 2);\n");
    CHECK(expanded); CHECK(expanded->find("42") != std::string::npos);
    expanded = preprocess("#if 1 + 1 == 2\nint answer(void) { return 42; }\n#else\nbad\n#endif\n");
    CHECK(expanded); CHECK(expanded->find("42") != std::string::npos); CHECK(expanded->find("bad") == std::string::npos);
    expanded = preprocess("#if 0\nint a(void) { return 0; }\n#elif 1\nint answer(void) { return 42; }\n#endif\n");
    CHECK(expanded); CHECK(expanded->find("answer") != std::string::npos);
    for (const auto* code : {"#if 1\n", "#include \"missing-header.h\"\n", "#error boom\n"}) {
        CHECK(!preprocess(code)); CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    // The internal engine keeps the minimal deterministic object-macro subset.
    expanded = preprocess("#define A 6\n#define B (A * 7)\n#ifdef A\nint answer(void) { return B; }\n#else\ninvalid\n#endif\n", cephyr::Preprocessor::Internal);
    CHECK(expanded); CHECK(expanded->find("(6 * 7)") != std::string::npos); CHECK(expanded->find("invalid") == std::string::npos);
    expanded = preprocess("#define A B\n#define B A\nA \"A\" /* A */\n", cephyr::Preprocessor::Internal);
    CHECK(expanded); CHECK(expanded->find("A \"A\"") != std::string::npos);
    expanded = preprocess("#define xFF 42\n0xFF\n", cephyr::Preprocessor::Internal);
    CHECK(expanded); CHECK(expanded->find("0xFF") != std::string::npos);
    expanded = preprocess("#if 0\n#if UNSUPPORTED\nignored\n#endif\n#endif\n", cephyr::Preprocessor::Internal);
    CHECK(expanded);
    for (const auto* code : {"#define F(x) x\n", "#if 1\n", "#if 0\n#elif 1\n#endif\n", "/*", "#include \"missing-header.h\"\n"}) {
        CHECK(!preprocess(code, cephyr::Preprocessor::Internal)); CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    // The system engine is available when a host preprocessor is installed.
    {
        cephyr::PreprocessOptions options; options.engine = cephyr::Preprocessor::System;
        auto probe = cephyr::preprocess(sources, sources.add("probe.c", "int probe(void) { return 1; }\n"), options, diagnostics);
        if (probe) {
            CHECK(probe->find("return 1") != std::string::npos);
            auto macros = cephyr::preprocess(sources, sources.add("sysmacros.c", "#define F(x) ((x) + 1)\nint answer(void) { return F(41); }\n"), options, diagnostics);
            CHECK(macros); CHECK(macros->find("41") != std::string::npos);
        } else diagnostics.clear(); // No host preprocessor: skip system checks.
    }
    auto file = sources.add("expanded.c", *preprocess("#define ADD(a, b) ((a) + (b))\nint answer(void) { return ADD(19, 23); }\n"));
    CHECK(cephyr::parse(sources, file, diagnostics));
    auto module = cephyr::lower(context, sources, file, diagnostics); CHECK(module);
    auto assembly = cephyr::emit_assembly(*module, diagnostics); CHECK(assembly); CHECK(assembly->find("mov $42, %eax") != std::string::npos);
    for (const auto* code : {"int f(void) { return ;", "int f(void) { return 2147483647 + 1; }", "int f(int x) { return y; }",
                            "int f(void) { return 1 << 40; }", "int f(void) { return 1 / 0; }", "int f(void) { return missing; }",
                            "int f(void) { return 1; } int f(void) { return 2; }", "int f(int a) { return a; } int f(int a, int b) { return a; }",
                            "int f(void) { return 1; } char *f(void);", "int f(void) { const int a = 1; a = 2; return a; }",
                            "int f(void) { int a[2]; a = 0; return 1; }", "int f(void) { int *p; return p & 1; }",
                            "int f(void) { struct S { int x; } s; return 0; }", "int f(int a, int b, int c, int d, int e, int g, int h) { return a; }",
                            "int f(void) { break; }", "int f(void) { return 1; } int f(void) { } void f(void);"}) {
        CHECK(!cephyr::lower(context, sources, sources.add("bad.c", code), diagnostics)); CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    auto general = sources.add("parse.c", "struct S { int x; }; int f(int x) { return x + 1; }");
    CHECK(cephyr::parse(sources, general, diagnostics));
    // The hosted scalar/pointer subset Cephyr lowers for libc: integers of every
    // width, pointers, arrays, globals, typedefs, and string literals.
    for (const auto* code : {
            "unsigned int f(void) { return 1; }",
            "long long f(long long a, unsigned short b) { return a + b; }",
            "int counter; const int limit = 7; char unit[4] = \"ab\"; typedef unsigned long size_t;",
            "int *p; int f(void) { int v = 3; p = &v; return *p + p[0]; }",
            "typedef unsigned long size_t; size_t f(void) { char buf[8]; buf[0] = 'x'; return sizeof buf; }",
            "char *f(void) { return \"cephyr\"; }",
            "int f(void) { int a[3]; int *q = a; q[1] = 5; return a[1] + (int)(a + 2 - a); }",
            "_Static_assert(sizeof(long) == 8, \"LP64\"); int f(void) { return 1; }"}) {
        auto accepted = sources.add("scalar.c", code);
        CHECK(cephyr::lower(context, sources, accepted, diagnostics));
        CHECK(!diagnostics.has_error());
        CHECK(cephyr::emit_assembly(cephyr::lower(context, sources, accepted, diagnostics).value(), diagnostics));
        diagnostics.clear();
    }
    auto dynamic = sources.add("dynamic.c", "int add(int x, int y) { int z=x+y; return z; } int answer(void) { return add(19,23); }");
    module = cephyr::lower(context, sources, dynamic, diagnostics); CHECK(module);
    const auto printed = mlvm::print_ir(*module);
    auto roundtrip = mlvm::parse_ir(context, sources, sources.add("roundtrip.mlvm", printed), diagnostics);
    CHECK(roundtrip); CHECK(mlvm::print_ir(*roundtrip) == printed);
    CHECK(cephyr::emit_assembly(*roundtrip, diagnostics));
    auto invalid = *module;
    mlvm::walk(invalid, [](mlvm::Operation& op) { if (op.name == "c.address_of") op.attributes["slot"] = "65535"; });
    CHECK(!mlvm::verify(invalid, diagnostics)); diagnostics.clear();
    invalid = *module;
    mlvm::walk(invalid, [](mlvm::Operation& op) { if (op.name == "c.call") op.attributes["sym"] = "missing"; });
    CHECK(!mlvm::verify(invalid, diagnostics)); diagnostics.clear();
    invalid = *module;
    mlvm::walk(invalid, [](mlvm::Operation& op) { if (op.name == "c.ptrload") op.name = "c.break"; });
    CHECK(!cephyr::emit_assembly(invalid, diagnostics)); diagnostics.clear();
    invalid = *module;
    mlvm::walk(invalid, [](mlvm::Operation& op) { if (op.name == "c.ptrload") op.operands.clear(); });
    CHECK(!mlvm::verify(invalid, diagnostics)); diagnostics.clear();
    CHECK(cephyr::parse(sources, sources.add("empty.c", ""), diagnostics));
    CHECK(!cephyr::parse(sources, sources.add("invalid.c", "int f(void) {\nreturn @;\n}"), diagnostics));
    CHECK(diagnostics.diagnostics().back().location.line == 2); diagnostics.clear();
    CHECK(cephyr::lower(context, sources, dynamic, diagnostics));
}); }
