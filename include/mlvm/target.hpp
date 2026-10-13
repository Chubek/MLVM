#pragma once
#include <mlvm/object.hpp>
#include <mlvm/ir.hpp>

namespace mlvm {
// Versioned target metadata describes capabilities, not inferred backend support.
bool verify_target(const Target&, DiagnosticEngine&);
std::optional<std::string> emit_llvm_ir(const Module&, DiagnosticEngine&);
std::optional<std::vector<std::uint8_t>> emit_wasm(const Module&, DiagnosticEngine&);
std::optional<Object> emit_x86_object(const Module&, DiagnosticEngine&);
std::optional<std::uint64_t> interpret(const Module&, std::span<const std::uint64_t>, DiagnosticEngine&);
} // namespace mlvm
