#include <cephyr/frontend.hpp>
#include <mlvm/c.hpp>

namespace cephyr {
std::optional<std::string> emit_assembly(const mlvm::Module& module, mlvm::DiagnosticEngine& diagnostics) {
    return mlvm::emit_c_x86_64(module, diagnostics);
}
}
