#pragma once
#include <mlvm/sched.hpp>

namespace cephyr {
// Registers the `cephyr` source action on a scheduler registry: one C
// translation unit becomes a verified `mlvm` artifact holding builtin, arith,
// func and c-dialect operations. Existing `pass`, `kernel`, `verify`, `text`
// and `target` actions consume that artifact unchanged; the x86-64 `target`
// route emits C-dialect modules through the C backend.
// The context must outlive the registry and every produced artifact, and must
// already have the builtin dialects registered.
bool register_scheduler_actions(mlvm::sched::Registry& registry, mlvm::Context& context);
} // namespace cephyr
