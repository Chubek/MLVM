#include <mlvm/mlvm.hpp>
#include <mlvm/object.hpp>
#ifdef MLVM_TEST_BINUTILS
#include <mlvm/binutils.hpp>
#endif
#ifdef MLVM_TEST_CEPHYR
#include <cephyr/frontend.hpp>
#endif
int main() {
    mlvm::Context context;
    mlvm::DiagnosticEngine diagnostics;
    if (!mlvm::register_builtin_dialects(context, diagnostics)) return 1;
    mlvm::Module module(context);
    if (!mlvm::verify(module, diagnostics)) return 2;
    if (!mlvm::write_elf(mlvm::Object{}, diagnostics)) return 3;
#ifdef MLVM_TEST_BINUTILS
    mlvm::BinaryObject object;
    object.sections.push_back({".bss", 8, 3, 8, 0, 16, {}});
    auto encoded = mlvm::write_binary_object(object, diagnostics);
    if (!encoded || !mlvm::read_binary_object(*encoded, diagnostics)) return 5;
    const std::vector<mlvm::ArchiveMember> members{{"long archive member name.bin", {1, 2, 3}}};
    if (!mlvm::write_indexed_archive(members, diagnostics)) return 6;
#endif
#ifdef MLVM_TEST_CEPHYR
    mlvm::SourceManager sources;
    auto compiled = cephyr::lower(context, sources, sources.add("consumer.c",
        "int add(int a, int b) { return a+b; } int answer(void) { return add(19,23); }"), diagnostics);
    if (!compiled) return 4;
    if (!cephyr::emit_assembly(*compiled, diagnostics)) return 7;
#ifdef MLVM_TEST_CEPHYR_SYSROOT
    cephyr::PreprocessOptions options;
    options.sysroot = MLVM_TEST_CEPHYR_SYSROOT;
    auto input = sources.add("runtime.c", "#include <stdlib.h>\n#include <inttypes.h>\nintmax_t f(void) { return strtoimax(\"42\", (char **)0, 10); }\n");
    auto expanded = cephyr::preprocess(sources, input, options, diagnostics);
    if (!expanded) return 8;
    auto runtime = cephyr::lower(context, sources, sources.add("runtime.i", *expanded), diagnostics);
    if (!runtime || !cephyr::emit_assembly(*runtime, diagnostics)) return 9;
#endif
#endif
    return 0;
}
