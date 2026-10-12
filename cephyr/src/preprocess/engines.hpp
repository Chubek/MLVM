#pragma once
#include <cephyr/frontend.hpp>

// Internal preprocessor engines. All engines share the contract of
// cephyr::preprocess: deterministic output on success, structured diagnostics
// and nullopt on failure, no process termination, no stdout/stderr writes.
namespace cephyr::preprocess_detail {
std::optional<std::string> preprocess_internal(mlvm::SourceManager& sources, mlvm::Identifier file,
                                               const PreprocessOptions& options,
                                               mlvm::DiagnosticEngine& diagnostics);
std::optional<std::string> preprocess_ucpp(mlvm::SourceManager& sources, mlvm::Identifier file,
                                           const PreprocessOptions& options,
                                           mlvm::DiagnosticEngine& diagnostics);
std::optional<std::string> preprocess_system(mlvm::SourceManager& sources, mlvm::Identifier file,
                                             const PreprocessOptions& options,
                                             mlvm::DiagnosticEngine& diagnostics);
} // namespace cephyr::preprocess_detail
