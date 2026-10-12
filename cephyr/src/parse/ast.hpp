#pragma once
#include <mlvm/core.hpp>

namespace cparser { class ASTNode; }
namespace cephyr::parser_detail {
// The AST is borrowed for the callback only. Diagnostics and lifetime management
// remain in the parser adapter; semantic analysis cannot retain parser nodes.
bool with_ast(mlvm::SourceManager& sources, mlvm::Identifier file,
              mlvm::DiagnosticEngine& diagnostics,
              const std::function<void(cparser::ASTNode*)>& callback);
}
