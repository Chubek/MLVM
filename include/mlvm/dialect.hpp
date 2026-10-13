#pragma once
#include <mlvm/conversion.hpp>

namespace mlvm {
// Every built-in dialect has an explicit operation contract. Registration is idempotent.
bool register_standard_dialects(Context&, DiagnosticEngine&);
// Integer expression lowering: high -> mid -> low, then a target-facing dialect.
ConversionGraph standard_conversions();
bool lower_dialect(Module&, std::string_view dialect, DiagnosticEngine&);
} // namespace mlvm
