#pragma once
#include <mlvm/ir.hpp>
#include <filesystem>

namespace cephyr {
enum class Preprocessor { Internal, Ucpp, System };
// Build-time libc selection; installed tools discover the sysroot relative to
// their executable. Embedders can explicitly select a relocated sysroot.
std::filesystem::path default_sysroot();
struct PreprocessOptions {
    std::vector<std::filesystem::path> include_paths;
    std::map<std::string, std::string> defines;
    std::set<std::string> undefines;
    std::vector<std::filesystem::path> forced_includes;
    // Ucpp is the default: full C99 preprocessing from vendored ucpp with
    // freestanding C11 predefined macros. System invokes an external
    // GCC-compatible preprocessor (auto-detected `cc` unless system_command
    // names one). Internal keeps the minimal deterministic object-macro engine.
    Preprocessor engine = Preprocessor::Ucpp;
    // Single executable path or bare name resolved via PATH. Empty selects
    // cc, gcc, clang, then cpp. Extra arguments are not accepted; pass -I/-D/
    // -U/-include through the matching option fields.
    std::string system_command;
    std::filesystem::path sysroot = default_sysroot();
    bool standard_includes = true;
};
// C11 freestanding. Preprocessed sources are registered separately for diagnostics.
std::optional<std::string> preprocess(mlvm::SourceManager& sources, mlvm::Identifier file,
                                     const PreprocessOptions& options, mlvm::DiagnosticEngine& diagnostics);
bool parse(mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics);
// cparser-iso11 AST -> typed MLVM integer/structured-C operations.
std::optional<mlvm::Module> lower(mlvm::Context& context, mlvm::SourceManager& sources,
                                mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics);
std::optional<std::string> emit_assembly(const mlvm::Module& module, mlvm::DiagnosticEngine& diagnostics);
} // namespace cephyr
