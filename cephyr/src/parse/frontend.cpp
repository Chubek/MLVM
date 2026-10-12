#include <cephyr/frontend.hpp>
#include <CParser.h>
#include "ast.hpp"
#include "cparser/error.hpp"
#include <algorithm>
#include <cstdio>

namespace cephyr {
namespace {
using namespace cparser;
class AstCleanup {
    std::set<ASTNode*> existing = ASTNode::cephyrLiveNodes;
public:
    ~AstCleanup() {
        for (;;) {
            auto it = std::find_if(ASTNode::cephyrLiveNodes.begin(), ASTNode::cephyrLiveNodes.end(),
                [&](ASTNode* node) { return !existing.contains(node) && node->getRefCount() == 0; });
            if (it == ASTNode::cephyrLiveNodes.end()) break;
            delete *it;
        }
    }
};

// The upstream lexer owns a FILE*. Supply a private stream, never stdin or a
// process-global temporary filename. Parser and AST lifetimes stay within a call.
class Input : public CLexer {
public:
    explicit Input(const std::string& text) : CLexer(nullptr) {
        _fp = std::tmpfile();
        if (!_fp) throw parser_detail::Error("cannot create parser input stream", 1);
        if (std::fwrite(text.data(), 1, text.size(), _fp) != text.size()) {
            std::fclose(_fp); _fp = nullptr;
            throw parser_detail::Error("cannot write parser input stream", 1);
        }
        std::rewind(_fp);
    }
};
}

bool parser_detail::with_ast(mlvm::SourceManager& sources, mlvm::Identifier file,
                            mlvm::DiagnosticEngine& diagnostics,
                            const std::function<void(cparser::ASTNode*)>& callback) {
    const auto* source = sources.file(file);
    if (!source || source->text.size() > 64 * 1024 * 1024 || source->text.find('\0') != std::string::npos) {
        diagnostics.error("cephyr.source", "invalid or oversized source"); return false;
    }
    try {
        (void)NullASTNode::getInstance();
        AstCleanup cleanup;
        Input input(source->text);
        CParser parser(&input);
        parser.parse(nullptr);
        callback(parser.getAST()->getRoot());
        return true;
    } catch (const parser_detail::Error& error) {
        diagnostics.error("cephyr.frontend", error.what(), {file, static_cast<unsigned>(std::max(1, error.line)), static_cast<unsigned>(std::max(1, error.column))});
        return false;
    }
}
bool parse(mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics) {
    return parser_detail::with_ast(sources, file, diagnostics, [](cparser::ASTNode*) {});
}
} // namespace cephyr
