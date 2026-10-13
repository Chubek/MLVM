#include <mlvm/object.hpp>
#include <mlvm/target.hpp>

namespace mlvm {
bool TargetRegistry::add(Target target) {
    DiagnosticEngine diagnostics;
    if (!verify_target(target, diagnostics)) return false;
    auto key = target.triple; return targets_.emplace(std::move(key), std::move(target)).second;
}
const Target* TargetRegistry::find(std::string_view triple) const {
    const auto it = targets_.find(triple); return it == targets_.end() ? nullptr : &it->second;
}
TargetRegistry TargetRegistry::builtins() {
    TargetRegistry registry;
    registry.add({"x86_64-unknown-linux-gnu", 62, 64, Endianness::little, "elf64", 1, "sysv-amd64", {"integer"}, {"machine", "c"}, true, true});
    registry.add({"wasm32-unknown-unknown", 0, 32, Endianness::little, "wasm", 1, "wasm", {"integer"}, {"wasm"}, false, true});
    registry.add({"llvmir-unknown-unknown", 0, 0, Endianness::unspecified, "llvm-ir", 1, "consumer-defined", {"integer"}, {"llvmir"}, false, true});
    registry.add({"interp-unknown-unknown", 0, 0, Endianness::unspecified, "none", 1, "mlvm-values", {"integer"}, {"interp"}, false, false});
    return registry;
}
std::vector<Target> TargetRegistry::list() const {
    std::vector<Target> result;
    for (const auto& [name, target] : targets_) { (void)name; result.push_back(target); }
    return result;
}
bool verify_target(const Target& target, DiagnosticEngine& d) {
    const bool abstract = target.object_format == "llvm-ir" || target.object_format == "none";
    if (target.schema_version != 1 || target.triple.empty() || target.triple.find('\0') != std::string::npos ||
        (target.pointer_bits != 16 && target.pointer_bits != 32 && target.pointer_bits != 64 && !(abstract && target.pointer_bits == 0)) ||
        (!abstract && target.endianness != Endianness::little && target.endianness != Endianness::big) || target.object_format.empty() ||
        (target.object_format.starts_with("elf") && !target.machine)) {
        d.error("target.description", "invalid version-1 target description"); return false;
    }
    return true;
}
} // namespace mlvm
