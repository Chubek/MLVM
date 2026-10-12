// MLVM pass bridge for Optilisp kernels.
//
// Two host interfaces meet here:
//   * ModuleOptimizer -- a host supplies stage, facts, and the actual
//     transformation for one kernel. kernel_pass() wraps one optimizer and
//     kernel into a Pass the PassManager can schedule with rollback.
//   * integer_optimizer -- the generic SSA integer backend shipped with MLVM.
//     It executes modular constant folding and the operand-forwarding
//     identities, and refuses everything it cannot prove.
//
// run_kernels() resolves a Module's embedded Optilisp declarations and its
// ordered kernel applications against a Blackstone directory. All requested
// kernels and IR edits commit together; successful execution consumes the
// applications while the definitions remain.

#include <mlvm/optilisp/pass.hpp>
#include <mlvm/optilisp/rewrite.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace mlvm::optilisp {
namespace {

// ---------------------------------------------------------------------------
// Integer helpers (MLVM arith semantics are two's-complement modular)
// ---------------------------------------------------------------------------

unsigned integer_width(const Module& module, ValueId value) {
    const auto* definition = module.value(value);
    if (definition == nullptr) return 0;
    const auto spelling = module.context->spelling(definition->type);
    if (spelling.size() < 2 || spelling[0] != 'i') return 0;
    unsigned width = 0;
    const auto parsed =
        std::from_chars(spelling.data() + 1, spelling.data() + spelling.size(), width);
    if (parsed.ec != std::errc{} || parsed.ptr != spelling.data() + spelling.size()) return 0;
    return width == 0 || width > 64 ? 0 : width;
}

std::uint64_t width_mask(unsigned width) {
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

std::int64_t sign_extend(std::uint64_t bits, unsigned width) {
    if (width >= 64) return std::bit_cast<std::int64_t>(bits);
    bits &= (std::uint64_t{1} << width) - 1;
    if (bits & (std::uint64_t{1} << (width - 1))) bits |= ~((std::uint64_t{1} << width) - 1);
    return std::bit_cast<std::int64_t>(bits);
}

bool read_constant(const Operation& op, std::uint64_t& out) {
    if (op.name != "arith.constant" || op.results.size() != 1) return false;
    const auto it = op.attributes.find("value");
    if (it == op.attributes.end()) return false;
    std::int64_t parsed = 0;
    const auto& text = it->second;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    out = static_cast<std::uint64_t>(parsed);
    return true;
}

// Locate the block owning an operation so it can be erased after use
// forwarding. Returns nullptr when the operation is not owned by any block.
Block* owning_block(Module& module, NodeId id) {
    std::function<Block*(Region&)> search = [&](Region& region) -> Block* {
        for (Block& block : region.blocks) {
            for (const Operation& op : block.operations) {
                if (op.id == id) return &block;
                for (auto& nested : op.regions)
                    if (Block* found = search(nested)) return found;
            }
        }
        return nullptr;
    };
    return search(module.body);
}

const Operation* find_definer(const Module& module, ValueId value) {
    const Operation* found = nullptr;
    walk(module, [&](const Operation& op) {
        for (auto result : op.results)
            if (result == value) found = &op;
    });
    return found;
}

// ---------------------------------------------------------------------------
// Optilisp constructor vocabulary understood by the generic backend
// ---------------------------------------------------------------------------

struct Constructor {
    std::string_view head;    // Optilisp pattern head
    std::string_view opcode;  // MLVM arith operation
    std::size_t operands;
};

// The vocabulary is deliberately small and explicit: only the integer arith
// identities the generic backend can prove on its own are executable. Any
// other kernel requires a host ModuleOptimizer.
const std::vector<Constructor>& constructors() {
    static const std::vector<Constructor> table{
        {"add", "arith.addi", 2},      {"addi", "arith.addi", 2},
        {"sub", "arith.subi", 2},      {"subi", "arith.subi", 2},
        {"mul", "arith.muli", 2},      {"muli", "arith.muli", 2},
        {"bitwise-and", "arith.and", 2}, {"bitwise-or", "arith.or", 2},
        {"bitwise-xor", "arith.xor", 2}, {"and", "arith.and", 2},
        {"or", "arith.or", 2},         {"xor", "arith.xor", 2}};
    return table;
}

const Constructor* lookup_constructor(std::string_view head) {
    for (const auto& entry : constructors())
        if (entry.head == head) return &entry;
    return nullptr;
}

// The fold vocabulary: (integer-add a b) and friends inside a result form.
struct FoldOperation {
    std::string_view head;
    std::string_view opcode;
};

const std::vector<FoldOperation>& fold_operations() {
    static const std::vector<FoldOperation> table{
        {"integer-add", "arith.addi"}, {"integer-sub", "arith.subi"},
        {"integer-mul", "arith.muli"}, {"integer-and", "arith.and"},
        {"integer-or", "arith.or"},    {"integer-xor", "arith.xor"}};
    return table;
}

const FoldOperation* lookup_fold(std::string_view head) {
    for (const auto& entry : fold_operations())
        if (entry.head == head) return &entry;
    return nullptr;
}

bool head_is(const Expr& form, std::string_view head) {
    return form.kind == ExprKind::list && !form.items.empty() && form.items[0].symbol(head);
}

bool symbol_is(const Expr& form, std::string_view text) {
    return form.kind == ExprKind::symbol && form.value == text;
}

// Preconditions and forbidden conditions the generic backend can decide from
// the IR alone. Every other identifier yields `unknown`, which never
// authorizes a rewrite (spec section 11.3).
Fact prove_condition(const Module& module, const Operation& op, std::string_view condition) {
    const auto* definition = module.context->operation(op.name);
    const bool pure = definition != nullptr && definition->effect == Effect::pure;
    if (condition == "operation-has-defined-compile-time-semantics") return Fact::proven_true;
    if (condition == "result-representable-under-integer-semantics") return Fact::proven_true;
    if (condition == "operand-has-compatible-integer-type") return Fact::proven_true;
    if (condition == "operation-has-observable-side-effects") return pure ? Fact::proven_false : Fact::unknown;
    if (condition == "signed-overflow-traps") return Fact::proven_false; // MLVM integers are modular
    if (condition == "unmodeled-observable-effects") return pure ? Fact::proven_false : Fact::unknown;
    (void)module;
    return Fact::unknown;
}

// ---------------------------------------------------------------------------
// Generic SSA integer backend
// ---------------------------------------------------------------------------

class IntegerOptimizer final : public ModuleOptimizer {
public:
    // Mutable only because ModuleOptimizer::fact is const; pass execution is
    // single-threaded and every entry is derived from committed rewrites.
    mutable std::map<std::string, Fact, std::less<>> history_;

    bool supports(const Kernel& kernel) const override {
        if (kernel.stage != Stage::hir) return false;
        const Expr& transform = kernel.transformation;
        if (transform.kind != ExprKind::list || transform.items.size() != 3) return false;
        if (!symbol_is(transform.items[0], "rewrite") && !symbol_is(transform.items[0], "fold"))
            return false;
        if (symbol_is(transform.items[0], "rewrite")) {
            const Expr& pattern = transform.items[1];
            if (pattern.kind != ExprKind::list || pattern.items.size() != 3) return false;
            const Constructor* constructor = lookup_constructor(pattern.items[0].value);
            // Only a repeated-operand identity is executable here: every other
            // shape needs a host proof of operand properties.
            if (constructor == nullptr) return false;
            if (pattern.items[1] != pattern.items[2]) return false;
            return symbol_is(transform.items[2], pattern.items[1].value);
        }
        // fold (op (integer a) (integer b)) (integer-result (integer-fop a b))
        const Expr& pattern = transform.items[1];
        const Expr& result = transform.items[2];
        if (pattern.kind != ExprKind::list || pattern.items.size() != 3) return false;
        if (lookup_constructor(pattern.items[0].value) == nullptr) return false;
        if (!head_is(result, "integer-result") || result.items.size() != 2) return false;
        const Expr& computation = result.items[1];
        if (computation.kind != ExprKind::list || computation.items.size() != 3) return false;
        const FoldOperation* fold = lookup_fold(computation.items[0].value);
        return fold != nullptr && fold->opcode == lookup_constructor(pattern.items[0].value)->opcode;
    }

    Stage stage(const Module&) const override { return Stage::hir; }

    Fact fact(std::string_view key, const Kernel& kernel, const Module& module,
              AnalysisManager&) const override {
        if (key.rfind("history:", 0) == 0) {
            const auto it = history_.find(std::string(key));
            return it == history_.end() ? Fact::unknown : it->second;
        }
        if (key.rfind("capability:", 0) == 0) return capability_fact(key.substr(11));
        if (key.rfind("analysis:", 0) == 0) {
            const std::string name(key.substr(9));
            // The backend computes constant values directly, so the analysis is
            // available for every arith module it accepts.
            if (name == "constant-value-analysis" || name == "reaching-definitions-analysis")
                return Fact::proven_true;
            return Fact::unknown;
        }
        (void)kernel;
        (void)module;
        return Fact::unknown;
    }

    bool apply(const Kernel& kernel, Module& module, AnalysisManager&,
               DiagnosticEngine& diagnostics) const override {
        const Expr& transform = kernel.transformation;
        if (!supports(kernel)) {
            diagnostics.error("optilisp.integer", "unsupported kernel '" + kernel.id + "'",
                              kernel.location);
            return false;
        }
        const bool fold = symbol_is(transform.items[0], "fold");
        std::size_t rewritten = 0;
        const bool ok = fold ? apply_fold(kernel, module, diagnostics, rewritten)
                             : apply_identity(kernel, module, diagnostics, rewritten);
        if (ok && rewritten == 0) return false; // inapplicable, not an error
        history_[kernel.id] = Fact::proven_true;
        module.rebuild_uses();
        return true;
    }

private:
    static Fact capability_fact(std::string_view name) {
        static const std::set<std::string, std::pair<Fact, bool>> known{
            {"typed-expressions", {Fact::proven_true, true}},
            {"integer-constant-operands", {Fact::proven_true, true}},
            {"constant-semantics", {Fact::proven_true, true}},
            {"integer-overflow-semantics", {Fact::proven_true, true}},
            {"modular-arithmetic", {Fact::proven_true, true}},
            {"wrapping-arithmetic", {Fact::proven_true, true}},
            {"bitwise-semantics", {Fact::proven_true, true}},
            {"boolean-semantics", {Fact::proven_true, true}},
            {"signed-arithmetic", {Fact::proven_true, true}},
            {"unsigned-arithmetic", {Fact::proven_true, true}},
            {"division-semantics", {Fact::proven_true, true}},
            {"shift-semantics", {Fact::proven_true, true}},
            {"comparison-semantics", {Fact::proven_true, true}},
            {"ssa-form", {Fact::proven_true, true}},
            {"explicit-control-flow", {Fact::proven_true, true}}};
        const auto it = known.find(std::string(name));
        if (it != known.end()) return it->second.first;
        return Fact::unknown;
    }

    // (rewrite (op x x) x): forward the operand, then erase the dead operation.
    bool apply_identity(const Kernel& kernel, Module& module, DiagnosticEngine& diagnostics,
                        std::size_t& rewritten) const {
        const Expr& pattern = kernel.transformation.items[1];
        const Constructor* constructor = lookup_constructor(pattern.items[0].value);
        std::vector<Operation*> matched;
        walk(module, [&](Operation& op) {
            if (op.name != constructor->opcode) return;
            if (op.operands.size() != constructor->operands) return;
            if (op.results.size() != 1) return;
            if (op.operands[0] != op.operands[1]) return;
            const auto* definition = module.context->operation(op.name);
            if (definition == nullptr || definition->effect != Effect::pure) return;
            // Types must agree; MLVM already requires identical i1..i64 types.
            if (integer_width(module, op.results[0]) == 0) return;
            if (integer_width(module, op.operands[0]) != integer_width(module, op.results[0])) return;
            matched.push_back(&op);
        });
        for (Operation* op : matched) {
            bool eligible = true;
            for (const auto& condition : kernel.preconditions) {
                const auto fact = prove_condition(module, *op, condition);
                // The generic backend only decides conditions it can prove;
                // unknown means inapplicable rather than malformed.
                if (fact == Fact::unknown) { eligible = false; break; }
                if (fact != Fact::proven_true) { eligible = false; break; }
            }
            if (!eligible) continue;
            for (const auto& condition : kernel.forbids) {
                if (prove_condition(module, *op, condition) != Fact::proven_false) {
                    eligible = false;
                    break;
                }
            }
            if (!eligible) continue;
            const auto result = op->results[0];
            const auto operand = op->operands[0];
            const auto id = op->id;
            if (!module.replace_uses(result, operand, diagnostics)) {
                diagnostics.error("optilisp.integer",
                                  "kernel '" + kernel.id + "' cannot forward the result", {});
                return false;
            }
            Block* block = owning_block(module, id);
            if (block == nullptr) {
                diagnostics.error("optilisp.integer", "kernel '" + kernel.id +
                                                          "' matched an operation outside any block",
                                  {});
                return false;
            }
            if (!module.erase(*block, id, diagnostics)) return false;
            ++rewritten;
        }
        return true;
    }

    // (fold (op (integer a) (integer b)) (integer-result (integer-fop a b)))
    bool apply_fold(const Kernel& kernel, Module& module, DiagnosticEngine& diagnostics,
                    std::size_t& rewritten) const {
        const Expr& pattern = kernel.transformation.items[1];
        const Constructor* constructor = lookup_constructor(pattern.items[0].value);
        std::vector<Operation*> matched;
        walk(module, [&](Operation& op) {
            if (op.name != constructor->opcode) return;
            if (op.operands.size() != constructor->operands || op.results.size() != 1) return;
            const auto* definition = module.context->operation(op.name);
            if (definition == nullptr || definition->effect != Effect::pure) return;
            for (auto operand : op.operands)
                if (integer_width(module, operand) == 0) return;
            if (integer_width(module, op.results[0]) == 0) return;
            matched.push_back(&op);
        });
        for (Operation* op : matched) {
            const unsigned width = integer_width(module, op->results[0]);
            std::uint64_t operands[2] = {0, 0};
            bool both_constant = true;
            for (std::size_t i = 0; i < 2; ++i) {
                const Operation* producer = find_definer(module, op->operands[i]);
                if (producer == nullptr || !read_constant(*producer, operands[i])) {
                    both_constant = false;
                    break;
                }
                if (integer_width(module, op->operands[i]) != width) { both_constant = false; break; }
            }
            // Not a constant expression: the kernel is simply inapplicable.
            if (!both_constant) continue;
            bool eligible = true;
            for (const auto& condition : kernel.preconditions) {
                const Fact fact = prove_condition(module, *op, condition);
                if (fact != Fact::proven_true) { eligible = false; break; }
            }
            if (!eligible) continue;
            for (const auto& condition : kernel.forbids) {
                if (prove_condition(module, *op, condition) != Fact::proven_false) {
                    eligible = false;
                    break;
                }
            }
            if (!eligible) continue;
            std::uint64_t result = 0;
            const std::string& name = op->name;
            if (name == "arith.addi") result = operands[0] + operands[1];
            else if (name == "arith.subi") result = operands[0] - operands[1];
            else if (name == "arith.muli") result = operands[0] * operands[1];
            else if (name == "arith.and") result = operands[0] & operands[1];
            else if (name == "arith.or") result = operands[0] | operands[1];
            else if (name == "arith.xor") result = operands[0] ^ operands[1];
            result &= width_mask(width);
            op->name = "arith.constant";
            op->operands.clear();
            op->attributes = {{"value", std::to_string(sign_extend(result, width))}};
            ++rewritten;
        }
        return true;
    }
};

std::vector<std::filesystem::path> olisp_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::is_directory(directory)) return files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".olisp")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

Pass kernel_pass(Kernel kernel, std::shared_ptr<ModuleOptimizer> optimizer) {
    const std::string name = "optilisp:" + kernel.id;
    return {name,
            {},
            {},
            {},
            {},
            [kernel = std::move(kernel), optimizer = std::move(optimizer)](
                Module& module, AnalysisManager& analyses, DiagnosticEngine& diagnostics) {
                if (!optimizer) {
                    diagnostics.error("optilisp.pass", "kernel pass has no optimizer");
                    return false;
                }
                if (!optimizer->supports(kernel)) {
                    diagnostics.error("optilisp.pass",
                                      "the selected backend cannot execute kernel '" + kernel.id +
                                          "'");
                    return false;
                }
                if (optimizer->stage(module) != kernel.stage) {
                    diagnostics.error("optilisp.pass",
                                      "kernel '" + kernel.id + "' targets stage '" +
                                          std::string(spelling(kernel.stage)) +
                                          "' but the module is at stage '" +
                                          std::string(spelling(optimizer->stage(module))) + "'");
                    return false;
                }
                // Requirements and history are checked before any edit: a
                // kernel that cannot run must not modify the IR.
                for (const auto& requirement : kernel.requirements) {
                    const auto key = requirement.kind + ":" + requirement.id;
                    if (optimizer->fact(key, kernel, module, analyses) != Fact::proven_true) {
                        diagnostics.error("optilisp.inapplicable",
                                          "kernel '" + kernel.id + "' requires '" + key +
                                              "'; the module does not provide it");
                        return false;
                    }
                }
                for (const auto& prior : kernel.prerequisites) {
                    if (optimizer->fact("history:" + prior, kernel, module, analyses) !=
                        Fact::proven_true) {
                        diagnostics.error("optilisp.inapplicable",
                                          "kernel '" + kernel.id + "' requires prior transformation '" +
                                              prior + "'");
                        return false;
                    }
                }
                for (const auto& prior : kernel.forbidden_prior) {
                    if (optimizer->fact("history:" + prior, kernel, module, analyses) !=
                        Fact::proven_false) {
                        diagnostics.error("optilisp.inapplicable",
                                          "kernel '" + kernel.id +
                                              "' must not run after transformation '" + prior + "'");
                        return false;
                    }
                }
                return optimizer->apply(kernel, module, analyses, diagnostics);
            }};
}

std::shared_ptr<ModuleOptimizer> integer_optimizer() {
    static const std::shared_ptr<IntegerOptimizer> optimizer = std::make_shared<IntegerOptimizer>();
    return optimizer;
}

bool run_kernels(Module& module, const std::filesystem::path& registry_directory,
                 const std::filesystem::path& kernel_directory, DiagnosticEngine& diagnostics) {
    const auto registry = load_registry(registry_directory);
    for (const auto& diagnostic : registry.diagnostics) {
        diagnostics.error(diagnostic.code, diagnostic.render(), {0, 0, 0});
        return false;
    }
    // Catalog scanning is permissive: an unrelated defect in the database
    // must not block a kernel the user actually requested. A requested kernel
    // is re-validated against the real registry before it runs.
    const Loader permissive({}, {}, Loader::Profile::blackstone);
    std::map<std::string, Kernel, std::less<>> catalog;
    std::map<std::string, std::string> catalog_files;
    std::map<std::string, Diagnostics> rejected;
    for (const auto& path : olisp_files(kernel_directory)) {
        const std::vector<std::filesystem::path> single{path};
        const auto loaded = permissive.load(single);
        if (!loaded.ok()) {
            rejected.emplace(path.string(), loaded.diagnostics);
            continue;
        }
        for (const auto& kernel : loaded.kernels) {
            catalog.emplace(kernel.id, kernel);
            catalog_files[kernel.id] = path.string();
        }
    }

    const Loader loader(registry.registry, {}, Loader::Profile::blackstone);
    std::map<std::string, Kernel, std::less<>> defined;
    for (const auto& source : module.kernel_sources) {
        const auto loaded = loader.load_source(source, "<kernel declaration>");
        for (const auto& diagnostic : loaded.diagnostics)
            diagnostics.error(diagnostic.code, diagnostic.render(), {0, 0, 0});
        if (!loaded.ok()) return false;
        for (auto& kernel : loaded.kernels) defined.insert_or_assign(kernel.id, std::move(kernel));
    }

    // Resolve the ordered applications before touching the IR.
    std::vector<Kernel> requested;
    std::set<std::string> seen;
    for (const auto& id : module.kernel_applications) {
        if (!seen.insert(id).second) {
            diagnostics.error("optilisp.duplicate", "kernel '" + id + "' is applied more than once");
            return false;
        }
        const auto local = defined.find(id);
        if (local != defined.end()) {
            requested.push_back(local->second);
            continue;
        }
        const auto entry = catalog.find(id);
        if (entry == catalog.end()) {
            std::string detail;
            if (const auto file = rejected.begin(); file != rejected.end() && !file->second.empty())
                detail = " (" + file->first + ": " + file->second.front().render() + ")";
            diagnostics.error("optilisp.unknown-kernel",
                              "no kernel '" + id + "' is declared or present in " +
                                  kernel_directory.string() + detail);
            return false;
        }
        Kernel kernel = entry->second;
        // Revalidate the catalog entry against the real registry before use:
        // the permissive scan above does not check references.
        for (const auto& requirement : kernel.requirements) {
            const bool known = requirement.kind == "capability"
                                   ? registry.registry.capabilities.contains(requirement.id)
                                   : registry.registry.analyses.contains(requirement.id);
            if (!known) {
                diagnostics.error("optilisp.kernel.reference",
                                  "kernel '" + id + "' (" + catalog_files[id] + ") references unknown " +
                                      requirement.kind + " '" + requirement.id + "'");
                return false;
            }
        }
        requested.push_back(std::move(kernel));
    }
    if (requested.empty()) {
        module.kernel_applications.clear();
        return true;
    }

    PassManager manager;
    for (const auto& kernel : requested)
        manager.add(kernel_pass(kernel, integer_optimizer()));
    if (!manager.run(module, diagnostics)) return false;
    module.kernel_applications.clear();
    return true;
}

} // namespace mlvm::optilisp