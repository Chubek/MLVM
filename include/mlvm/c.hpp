#pragma once
#include <mlvm/ir.hpp>

namespace mlvm {
// Structured, signed-i32 C operations. Locals are numbered function-frame slots;
// control regions explicitly yield their values. No target registers occur here.
bool register_c_dialect(Context& context, DiagnosticEngine& diagnostics);
// Cross-operation contracts: signatures, local ownership, and control transfers.
bool verify_c_module(const Module& module, DiagnosticEngine& diagnostics);
std::optional<std::string> emit_c_x86_64(const Module& module, DiagnosticEngine& diagnostics);
}
