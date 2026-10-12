// Executable Optilisp tree rewriting.
//
// This is the language's own evaluator for declarative rewrite rules. It
// operates on value-owned Expr terms, not on NodeIR; see
// mlvm/optilisp/rewrite.hpp for the stage-typed NodeIR backends that host
// compilers can use instead.
//
// Rule semantics (documented, not inferred):
//   * constructor heads are literal symbols,
//   * a symbol operand position is a variable binding,
//   * numbers, strings, booleans, keywords, and nil are literals,
//   * repeated variables must bind the same value,
//   * every variable in a replacement must be bound by the pattern.
//
// Rewriting runs preorder over the term to a fixed point. Term and facts
// commit together: any exception, cycle detection, or budget exhaustion
// discards the candidate and leaves the caller's term untouched.

#include <mlvm/optilisp/rewrite.hpp>

#include <algorithm>
#include <set>

namespace mlvm::optilisp {
namespace {

constexpr std::size_t default_budget = 64;

bool is_literal_atom(const Expr& expr) {
    return expr.kind == ExprKind::keyword || expr.kind == ExprKind::string ||
           expr.kind == ExprKind::number || expr.kind == ExprKind::boolean ||
           expr.kind == ExprKind::nil;
}

// Count AST nodes against ParseLimits before compiling or rewriting.
void charge(const Expr& expr, const ParseLimits& limits, std::size_t& nodes) {
    if (++nodes > limits.max_nodes) throw std::runtime_error("Optilisp term node limit exceeded");
    if (expr.value.size() > limits.max_bytes - std::min(nodes, limits.max_bytes))
        throw std::runtime_error("Optilisp term byte limit exceeded");
    for (const auto& item : expr.items) charge(item, limits, nodes);
}

// Structural matching is deterministic: each variable position binds the
// single subject node it reaches, so a repeated variable either agrees or the
// whole match fails. No backtracking is required or performed.
struct Matcher {
    const Expr& pattern;
    const Expr& subject;
    Bindings bindings;

    bool visit(const Expr& p, const Expr& s) {
        if (p.kind == ExprKind::symbol) {
            const auto [it, inserted] = bindings.emplace(p.value, s);
            return inserted || it->second == s;
        }
        if (is_literal_atom(p)) return p == s;
        // Constructor application: head must match and arity must agree.
        if (s.kind != ExprKind::list || p.items.empty() || s.items.empty()) return false;
        if (s.items[0] != p.items[0]) return false;
        if (s.items.size() != p.items.size()) return false;
        for (std::size_t i = 1; i < p.items.size(); ++i)
            if (!visit(p.items[i], s.items[i])) return false;
        return true;
    }
};

Expr instantiate(const Expr& replacement, const Bindings& bindings) {
    std::function<Expr(const Expr&)> build = [&](const Expr& e) -> Expr {
        if (e.kind == ExprKind::symbol) {
            const auto it = bindings.find(e.value);
            if (it == bindings.end())
                throw std::runtime_error("unbound Optilisp replacement variable: " + e.value);
            return it->second;
        }
        Expr copy = e;
        for (auto& item : copy.items) item = build(item);
        return copy;
    };
    return build(replacement);
}

// A kernel is executable here only when it states an explicit two-argument
// rewrite over constructor applications. Everything else needs a host
// ModuleOptimizer.
bool pattern_rewrite(const Kernel& kernel, Expr& pattern, Expr& result) {
    const Expr& transform = kernel.transformation;
    if (transform.kind != ExprKind::list || transform.items.size() != 3) return false;
    if (!transform.items[0].symbol("rewrite")) return false;
    pattern = transform.items[1];
    result = transform.items[2];
    return pattern.kind == ExprKind::list && !pattern.items.empty();
}

class RewriteSystem::Batch final : public Installation {
    RewriteSystem& owner_;
    std::vector<Kernel> pending_;
    bool committed_ = false;

public:
    explicit Batch(RewriteSystem& owner) : owner_(owner) {}

    bool add(const Kernel& kernel, Diagnostics& diagnostics) override {
        if (committed_) {
            diagnostics.push_back({"optilisp.rewrite", "installation already committed", {}});
            return false;
        }
        Expr pattern, result;
        if (!pattern_rewrite(kernel, pattern, result)) {
            diagnostics.push_back({"optilisp.rewrite",
                                  "kernel '" + kernel.id +
                                      "' does not describe an executable rewrite",
                                  kernel.location});
            return false;
        }
        if (kernel.preconditions.empty() || kernel.forbids.empty() || kernel.requirements.empty()) {
            diagnostics.push_back({"optilisp.rewrite",
                                  "kernel '" + kernel.id +
                                      "' lacks a complete applicability contract",
                                  kernel.location});
            return false;
        }
        // Every replacement variable must be bound by the pattern.
        std::set<std::string> bound;
        std::function<void(const Expr&, std::size_t)> collect = [&](const Expr& e, std::size_t depth) {
            if (depth > 128) throw std::runtime_error("Optilisp pattern nesting too deep");
            if (e.kind == ExprKind::symbol) {
                bound.insert(e.value);
                return;
            }
            for (const auto& item : e.items) collect(item, depth + 1);
        };
        try {
            std::size_t nodes = 0;
            charge(pattern, {}, nodes);
            charge(result, {}, nodes);
            collect(result, 1);
        } catch (const std::exception& error) {
            diagnostics.push_back(
                {"optilisp.rewrite", kernel.id + ": " + error.what(), kernel.location});
            return false;
        }
        // Substitute the result and confirm every variable is bound by the pattern.
        std::function<void(const Expr&, std::size_t)> check = [&](const Expr& e, std::size_t depth) {
            if (depth > 128) throw std::runtime_error("Optilisp result nesting too deep");
            if (e.kind == ExprKind::symbol) {
                if (!bound.count(e.value))
                    throw std::runtime_error("unbound Optilisp result variable: " + e.value);
                return;
            }
            for (const auto& item : e.items) check(item, depth + 1);
        };
        try {
            check(result, 1);
        } catch (const std::exception& error) {
            diagnostics.push_back(
                {"optilisp.rewrite", kernel.id + ": " + error.what(), kernel.location});
            return false;
        }
        for (const auto& existing : owner_.kernels_) {
            if (existing.id == kernel.id) {
                diagnostics.push_back({"optilisp.rewrite",
                                      "kernel '" + kernel.id + "' is already installed", {}});
                return false;
            }
        }
        pending_.push_back(kernel);
        return true;
    }

    bool commit(Diagnostics& diagnostics) override {
        if (committed_) {
            diagnostics.push_back({"optilisp.rewrite", "installation already committed", {}});
            return false;
        }
        std::sort(pending_.begin(), pending_.end(),
                  [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
        owner_.kernels_ = std::move(pending_);
        committed_ = true;
        return true;
    }
};

} // namespace

std::unique_ptr<Installation> RewriteSystem::begin_installation(Diagnostics&) {
    return std::make_unique<Batch>(*this);
}

OptimizeResult RewriteSystem::optimize(Expr& term, RewriteContext& context,
                                       std::size_t rewrite_budget, ParseLimits limits) const {
    OptimizeResult result;
    if (rewrite_budget == 0) rewrite_budget = default_budget;
    Expr candidate = term;
    Facts facts = context.facts;
    try {
        std::set<std::string> seen{candidate.print()};
        std::size_t rewrites = 0;
        bool changed = false;
        do {
            changed = false;
            std::function<bool(const Expr&)> descend = [&](const Expr& node) -> bool {
                for (const auto& item : node.items) {
                    if (descend(item)) return true;
                }
                for (const auto& kernel : kernels_) {
                    // Global applicability: requirements and history facts.
                    bool eligible = true;
                    for (const auto& requirement : kernel.requirements)
                        eligible &= facts.get(requirement.kind + ":" + requirement.id) ==
                                    Fact::proven_true;
                    for (const auto& prior : kernel.prerequisites)
                        eligible &= facts.get("history:" + prior) == Fact::proven_true;
                    for (const auto& prior : kernel.forbidden_prior)
                        eligible &= facts.get("history:" + prior) == Fact::proven_false;
                    if (!eligible) continue;
                    Expr pattern, replacement;
                    if (!pattern_rewrite(kernel, pattern, replacement)) continue;
                    Matcher matcher{pattern, node, {}};
                    if (!matcher.visit(pattern, node)) continue;
                    const Bindings bindings = std::move(matcher.bindings);
                    const Expr proposed = instantiate(replacement, bindings);
                    if (proposed == node) continue;
                    const std::string fingerprint = proposed.print();
                    if (fingerprint == node.print()) continue;
                    // Candidate-specific proof obligations. A missing prover or
                    // an unproven condition means inapplicable, not an error.
                    ProofRequest request{kernel, node, proposed, bindings};
                    if (context.prove) {
                        for (const auto& condition : kernel.preconditions) {
                            if (context.prove(request, condition) != Fact::proven_true) {
                                eligible = false;
                                break;
                            }
                        }
                        if (!eligible) continue;
                        for (const auto& condition : kernel.forbids) {
                            if (context.prove(request, condition) != Fact::proven_false) {
                                eligible = false;
                                break;
                            }
                        }
                        if (!eligible) continue;
                    }
                    if (++rewrites > rewrite_budget)
                        throw std::runtime_error("Optilisp rewrite budget exhausted; changes discarded");
                    if (!seen.insert(fingerprint).second)
                        throw std::runtime_error("Optilisp rewrite cycle; changes discarded");
                    candidate = proposed;
                    facts.invalidate(kernel.analyses);
                    facts.set("history:" + kernel.id, Fact::proven_true);
                    changed = true;
                    return true;
                }
                return false;
            };
            changed = descend(candidate);
        } while (changed);
        std::size_t nodes = 0;
        charge(candidate, limits, nodes);
        term = std::move(candidate);
        context.facts = std::move(facts);
        result.success = true;
        result.rewrites = rewrites;
    } catch (const std::exception& error) {
        result.diagnostics.push_back({"optilisp.rewrite", error.what(), {}});
    }
    return result;
}

} // namespace mlvm::optilisp