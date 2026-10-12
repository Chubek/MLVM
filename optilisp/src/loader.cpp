// Optilisp kernel registry, loader, validation, and facts (Layers 2-4).
//
// Two responsibilities live here, kept separate:
//   * parse-level shape checks (required fields, duplicate sections,
//     recognized stages, verb vocabulary), and
//   * reference/semantic checks against a Registry (capabilities, analyses).
//
// Profile::strict  -- every well-formedness rule enforced (used for new
//                     definitions and for tests).
// Profile::blackstone -- accepts the upstream 150-kernel database, which omits
//                     analysis-contract sections, while still enforcing every
//                     structural and reference rule.

#include <mlvm/optilisp/loader.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace mlvm::optilisp {
namespace {

// ---------------------------------------------------------------------------
// Closed vocabularies (see libstone/blackstone/docs/optilisp.md)
// ---------------------------------------------------------------------------

const std::set<std::string>& stage_names() {
    static const std::set<std::string> names{"hir", "mir", "lir"};
    return names;
}

const std::set<std::string>& transform_verbs() {
    static const std::set<std::string> verbs{
        "rewrite",      "fold",         "propagate",     "eliminate",   "hoist",
        "sink",         "interchange",  "unroll",        "peel",        "fuse",
        "distribute",   "tile",         "skew",          "inline",      "specialize",
        "devirtualize", "lower",        "select",        "combine",     "promote",
        "demote",       "coalesce",     "rematerialize", "forward",     "canonicalize",
        "simplify",     "split",        "merge",         "rotate",      "vectorize",
        "parallelize",  "if-convert",   "construct",     "outline",     "place",
        "schedule",     "predict",      "deduplicate",   "remove",      "strength-reduce"};
    return verbs;
}

const std::map<std::string, std::set<std::string>>& stage_verbs() {
    static const std::map<std::string, std::set<std::string>> table{
        {"hir", {"rewrite", "fold", "propagate", "eliminate", "hoist", "sink", "interchange",
                 "unroll", "peel", "fuse", "distribute", "tile", "inline", "specialize",
                 "canonicalize", "simplify", "split", "merge", "deduplicate", "remove",
                 "strength-reduce"}},
        {"mir", {"rewrite", "fold", "propagate", "eliminate", "hoist", "sink", "interchange",
                 "unroll", "peel", "fuse", "distribute", "tile", "skew", "inline", "specialize",
                 "devirtualize", "promote", "forward", "canonicalize", "simplify", "split",
                 "merge", "vectorize", "parallelize", "if-convert", "construct", "outline",
                 "deduplicate", "remove", "strength-reduce", "coalesce"}},
        {"lir", {"rewrite", "fold", "eliminate", "hoist", "sink", "lower", "select", "combine",
                 "promote", "demote", "coalesce", "rematerialize", "canonicalize", "simplify",
                 "split", "merge", "place", "schedule", "deduplicate", "remove",
                 "strength-reduce"}}};
    return table;
}

// Sections accepted inside define-kernel, in canonical order.
const std::set<std::string>& kernel_sections() {
    static const std::set<std::string> sections{
        "category",       "paradigms",        "objective",
        "requires",       "preconditions",    "forbids",
        "prerequisites",  "forbidden-prior-transformations",
        "analysis-contract", "transform",    "correctness",
        "expected-effects", "limitations",   "documentation"};
    return sections;
}

// Sections required by the strict profile.
const std::set<std::string>& required_sections() {
    static std::set<std::string> required;
    if (required.empty())
        required = {"category", "objective", "requires", "preconditions", "transform",
                    "correctness", "expected-effects", "limitations", "documentation"};
    return required;
}

bool stage_from(std::string_view text, Stage& out) {
    if (text == "hir") { out = Stage::hir; return true; }
    if (text == "mir") { out = Stage::mir; return true; }
    if (text == "lir") { out = Stage::lir; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Small helpers over the value-owned Expr tree
// ---------------------------------------------------------------------------

bool head_is(const Expr& form, std::string_view head) {
    return form.kind == ExprKind::list && !form.items.empty() && form.items[0].symbol(head);
}

std::string_view head_name(const Expr& form) {
    if (form.kind != ExprKind::list || form.items.empty() ||
        form.items[0].kind != ExprKind::symbol)
        return {};
    return form.items[0].value;
}

// A condition list is either a bare symbol ``(a b c)`` or a list of one-element
// lists ``((a) (b))``. Upstream uses the latter, the MLVM supplement the former.
void collect_conditions(const Expr& section, std::vector<std::string>& out) {
    for (std::size_t i = 1; i < section.items.size(); ++i) {
        const Expr& item = section.items[i];
        if (item.kind == ExprKind::symbol) out.push_back(item.value);
        else if (head_name(item) != std::string_view{}) out.emplace_back(item.items[0].value);
    }
}

// Identifiers appear as bare symbols or as strings.
void collect_identifiers(const Expr& section, std::vector<std::string>& out) {
    for (std::size_t i = 1; i < section.items.size(); ++i) {
        const Expr& item = section.items[i];
        if (item.kind == ExprKind::symbol || item.kind == ExprKind::string)
            out.push_back(item.value);
    }
}

std::string join_strings(const Expr& section) {
    std::string out;
    for (std::size_t i = 1; i < section.items.size(); ++i) {
        if (section.items[i].kind != ExprKind::string) continue;
        if (!out.empty()) out += ' ';
        out += section.items[i].value;
    }
    return out;
}

struct LoaderContext {
    const Registry* registry = nullptr;
    Loader::Profile profile = Loader::Profile::strict;
    ParseLimits limits;
    Diagnostics* diagnostics = nullptr;

    void error(std::string code, std::string message, Location location) const {
        diagnostics->push_back({std::move(code), std::move(message), location});
    }
};

// (requires (capability x) (analysis y))
void parse_requires(const Expr& section, Kernel& kernel, const LoaderContext& ctx) {
    for (std::size_t i = 1; i < section.items.size(); ++i) {
        const Expr& item = section.items[i];
        const auto head = head_name(item);
        if (head != "capability" && head != "analysis") {
            ctx.error("optilisp.kernel.requirement",
                      "expected (capability ID) or (analysis ID)", item.location);
            continue;
        }
        if (item.items.size() != 2 ||
            (item.items[1].kind != ExprKind::symbol && item.items[1].kind != ExprKind::string)) {
            ctx.error("optilisp.kernel.requirement",
                      "requirement requires exactly one identifier", item.location);
            continue;
        }
        const auto& id = item.items[1].value;
        if (id.empty()) {
            ctx.error("optilisp.kernel.requirement", "requirement identifier is empty",
                      item.location);
            continue;
        }
        const std::string kind(head);
        const bool known = kind == "capability"
                               ? ctx.registry->capabilities.contains(id)
                               : ctx.registry->analyses.contains(id);
        if (!known)
            ctx.error("optilisp.kernel.reference",
                      "unresolved " + kind + " identifier '" + id + "' referenced by kernel '" +
                          kernel.id + "'",
                      item.location);
        else if (kind == "capability") {
            const auto& stages = ctx.registry->capabilities.at(id);
            if (!stages.empty() && stages.count(kernel.stage) == 0)
                ctx.error("optilisp.kernel.stage",
                          "capability '" + id + "' does not apply to stage '" +
                              std::string(spelling(kernel.stage)) + "'",
                          item.location);
        }
        kernel.requirements.push_back({kind, id});
    }
}

void parse_contract(const Expr& section, Kernel& kernel, const LoaderContext& ctx) {
    for (std::size_t i = 1; i < section.items.size(); ++i) {
        const Expr& clause = section.items[i];
        const auto head = head_name(clause);
        std::set<std::string>* target = nullptr;
        if (head == "requires") target = &kernel.analyses.requires_analyses;
        else if (head == "invalidates") target = &kernel.analyses.invalidates;
        else if (head == "preserves") target = &kernel.analyses.preserves;
        else {
            ctx.error("optilisp.kernel.contract",
                      "expected requires, invalidates, or preserves", clause.location);
            continue;
        }
        std::vector<std::string> ids;
        collect_identifiers(clause, ids);
        for (const auto& id : ids) {
            if (!ctx.registry->analyses.contains(id))
                ctx.error("optilisp.kernel.reference",
                          "unresolved analysis identifier '" + id + "' in analysis contract of '" +
                              kernel.id + "'",
                          clause.location);
            target->insert(id);
        }
    }
    const auto& c = kernel.analyses;
    for (const auto& id : c.invalidates) {
        if (c.preserves.contains(id))
            ctx.error("optilisp.kernel.contradiction",
                      "kernel '" + kernel.id + "' both invalidates and preserves '" + id + "'",
                      section.location);
    }
}

bool parse_kernel(const Expr& form, Kernel& kernel, const LoaderContext& ctx) {
    if (form.items.empty()) {
        ctx.error("optilisp.kernel.empty", "empty define-kernel form", form.location);
        return false;
    }
    kernel.location = form.location;

    // Leading :keyword value pairs, then body sections. Nothing may follow.
    std::set<std::string> seen_keywords;
    std::map<std::string, const Expr*, std::less<>> keywords;
    std::vector<const Expr*> sections;
    std::size_t i = 1;
    bool in_body = false;
    bool ok = true;
    for (; i < form.items.size(); ++i) {
        const Expr& item = form.items[i];
        if (item.kind == ExprKind::keyword) {
            if (in_body) {
                ctx.error("optilisp.kernel.field", "keyword '" + item.value +
                                                       "' appears after a body section",
                          item.location);
                return false;
            }
            if (i + 1 >= form.items.size()) {
                ctx.error("optilisp.kernel.field", "keyword '" + item.value +
                                                       "' requires a value",
                          item.location);
                return false;
            }
            if (!seen_keywords.insert(item.value).second) {
                ctx.error("optilisp.kernel.field", "duplicate keyword '" + item.value + "'",
                          item.location);
                return false;
            }
            keywords.emplace(item.value, &form.items[i + 1]);
            ++i; // consume the value
            continue;
        }
        if (item.kind == ExprKind::list && !item.items.empty()) {
            in_body = true;
            sections.push_back(&item);
            continue;
        }
        ctx.error("optilisp.kernel.field",
                  "unexpected item outside a keyword argument or body section", item.location);
        return false;
    }
    if (keywords.empty() || sections.empty()) {
        ctx.error("optilisp.kernel.field", "define-kernel requires keywords and body sections",
                  form.location);
        return false;
    }

    // Kernel identity keywords.
    for (const char* required : {"id", "name", "stage"}) {
        if (!keywords.contains(required))
            ctx.error("optilisp.kernel.field",
                      std::string("kernel is missing a :") + required + " declaration",
                      form.location);
    }
    auto keyword_text = [&](const char* name) -> std::string {
        const auto it = keywords.find(name);
        if (it == keywords.end() || !it->second) return {};
        return it->second->kind == ExprKind::string ? it->second->value : std::string{};
    };
    kernel.id = keyword_text("id");
    kernel.name = keyword_text("name");
    if (!keyword_text("id").empty() && keywords.at("id")->kind != ExprKind::string)
        ctx.error("optilisp.kernel.field", ":id must be a string", keywords.at("id")->location);
    if (!keyword_text("name").empty() && keywords.at("name")->kind != ExprKind::string)
        ctx.error("optilisp.kernel.field", ":name must be a string", keywords.at("name")->location);

    const auto stage_it = keywords.find("stage");
    std::string stage_text;
    if (stage_it != keywords.end() && stage_it->second)
        stage_text = stage_it->second->kind == ExprKind::symbol ? stage_it->second->value
                                                                : std::string{};
    if (stage_text.empty()) {
        ctx.error("optilisp.kernel.stage",
                  "kernel is missing a :stage declaration (hir, mir, or lir)", form.location);
        ok = false;
    } else if (!stage_from(stage_text, kernel.stage)) {
        ctx.error("optilisp.kernel.stage",
                  "unrecognized stage '" + stage_text + "' (expected hir, mir, or lir)",
                  stage_it->second->location);
        ok = false;
    }
    if (kernel.id.empty() || kernel.name.empty()) ok = false;

    // Sections: reject unknown and duplicate heads.
    std::map<std::string, const Expr*, std::less<>> by_head;
    for (const Expr* section : sections) {
        const auto head = head_name(*section);
        if (head.empty()) {
            ctx.error("optilisp.kernel.section", "expected a named section", section->location);
            ok = false;
            continue;
        }
        const std::string name(head);
        if (kernel_sections().count(name) == 0) {
            ctx.error("optilisp.kernel.section", "unknown section '" + name + "'",
                      section->location);
            ok = false;
            continue;
        }
        if (!by_head.emplace(name, section).second) {
            ctx.error("optilisp.kernel.section", "duplicate '" + name + "' section",
                      section->location);
            ok = false;
        }
    }
    for (const auto& required : required_sections()) {
        if (by_head.contains(required)) continue;
        // The upstream database predates analysis-contract; only tolerate it.
        if (required == "analysis-contract" && ctx.profile == Loader::Profile::blackstone) continue;
        ctx.error("optilisp.kernel.section", "kernel is missing a (" + required + " ...) section",
                  form.location);
        ok = false;
    }
    if (ctx.profile == Loader::Profile::blackstone && !by_head.contains("analysis-contract"))
        kernel.analyses.requires_analyses.clear();

    // Field extraction.
    if (const auto it = by_head.find("category"); it != by_head.end()) {
        std::vector<std::string> categories;
        collect_conditions(*it->second, categories);
        if (categories.empty())
            ctx.error("optilisp.kernel.category", "kernel '" + kernel.id +
                                                      "' must declare a nonempty category",
                      it->second->location);
        else if (categories.size() > 1 && ctx.profile == Loader::Profile::strict)
            ctx.error("optilisp.kernel.objective",
                      "kernel '" + kernel.id +
                          "' declares multiple categories; one primary objective is required",
                      it->second->location);
        kernel.category = categories.empty() ? std::string{} : categories.front();
    }
    if (const auto it = by_head.find("paradigms"); it != by_head.end())
        collect_conditions(*it->second, kernel.paradigms);
    if (const auto it = by_head.find("objective"); it != by_head.end())
        kernel.objective = join_strings(*it->second);
    if (const auto it = by_head.find("requires"); it != by_head.end())
        parse_requires(*it->second, kernel, ctx);
    if (const auto it = by_head.find("preconditions"); it != by_head.end())
        collect_conditions(*it->second, kernel.preconditions);
    if (const auto it = by_head.find("forbids"); it != by_head.end())
        collect_conditions(*it->second, kernel.forbids);
    if (const auto it = by_head.find("prerequisites"); it != by_head.end()) {
        for (std::size_t k = 1; k < it->second->items.size(); ++k) {
            const Expr& entry = it->second->items[k];
            if (head_name(entry) == "optimization-applied" && entry.items.size() == 2 &&
                entry.items[1].kind == ExprKind::string)
                kernel.prerequisites.insert(entry.items[1].value);
            else
                ctx.error("optilisp.kernel.dependency",
                          "expected (optimization-applied \"kernel-id\")", entry.location);
        }
    }
    if (const auto it = by_head.find("forbidden-prior-transformations"); it != by_head.end()) {
        std::vector<std::string> forbidden;
        collect_identifiers(*it->second, forbidden);
        kernel.forbidden_prior.insert(forbidden.begin(), forbidden.end());
    }
    for (const auto& both : kernel.prerequisites) {
        if (kernel.forbidden_prior.count(both))
            ctx.error("optilisp.kernel.contradiction",
                      "kernel '" + kernel.id + "' requires and forbids prior transformation '" +
                          both + "'",
                      form.location);
    }
    if (const auto it = by_head.find("analysis-contract"); it != by_head.end())
        parse_contract(*it->second, kernel, ctx);

    // The transformation is retained verbatim for host backends.
    if (const auto it = by_head.find("transform"); it != by_head.end()) {
        const Expr& section = *it->second;
        if (section.items.size() != 2) {
            ctx.error("optilisp.kernel.transform",
                      "transform section must contain exactly one transformation", section.location);
            ok = false;
        } else {
            const Expr& expression = section.items[1];
            kernel.transformation = expression;
            const auto verb = head_name(expression);
            if (verb.empty()) {
                ctx.error("optilisp.kernel.transform",
                          "expected a transformation form with a verb", expression.location);
                ok = false;
            } else if (transform_verbs().count(std::string(verb)) == 0) {
                ctx.error("optilisp.kernel.transform", "unknown transformation verb '" +
                                                           std::string(verb) + "'",
                          expression.location);
                ok = false;
            } else if (expression.items.size() < 2) {
                ctx.error("optilisp.kernel.transform",
                          "transformation verb '" + std::string(verb) + "' requires arguments",
                          expression.location);
                ok = false;
            } else {
                const auto stage_table = stage_verbs().find(std::string(spelling(kernel.stage)));
                if (stage_table != stage_verbs().end() &&
                    !stage_table->second.count(verb)) {
                    ctx.error("optilisp.kernel.stage",
                              "transformation verb '" + std::string(verb) +
                                  "' is not valid at stage '" + std::string(spelling(kernel.stage)) +
                                  "'",
                              expression.location);
                    if (ctx.profile == Loader::Profile::strict) ok = false;
                }
                if ((verb == "rewrite" || verb == "fold") && expression.items.size() != 3) {
                    ctx.error("optilisp.kernel.transform",
                              "rewrite and fold require exactly a pattern and a result",
                              expression.location);
                    ok = false;
                }
            }
        }
    }
    if (const auto it = by_head.find("correctness"); it != by_head.end())
        collect_conditions(*it->second, kernel.correctness);
    if (const auto it = by_head.find("expected-effects"); it != by_head.end())
        kernel.expected_effects = join_strings(*it->second);
    if (const auto it = by_head.find("limitations"); it != by_head.end())
        kernel.limitations = join_strings(*it->second);
    if (const auto it = by_head.find("documentation"); it != by_head.end()) {
        for (std::size_t k = 1; k < it->second->items.size(); ++k) {
            const Expr& entry = it->second->items[k];
            if (entry.kind == ExprKind::string) continue; // prose paragraph
            const auto topic = head_name(entry);
            if (topic.empty()) {
                ctx.error("optilisp.kernel.documentation", "expected a documented topic",
                          entry.location);
                ok = false;
                continue;
            }
            if (entry.items.size() == 2 && entry.items[1].kind == ExprKind::string) {
                if (!kernel.documentation.emplace(std::string(topic), entry.items[1].value).second)
                    ctx.error("optilisp.kernel.documentation",
                              "duplicate documentation topic '" + std::string(topic) + "'",
                              entry.location);
            } else {
                ctx.error("optilisp.kernel.documentation",
                          "documentation topic '" + std::string(topic) +
                              "' requires exactly one nonempty string",
                          entry.location);
                ok = false;
            }
        }
    }

    // Nonempty obligations required by both profiles.
    if (kernel.objective.empty())
        ctx.error("optilisp.kernel.objective", "kernel '" + kernel.id +
                                                   "' must declare a primary objective",
                  form.location);
    if (kernel.requirements.empty())
        ctx.error("optilisp.kernel.requires", "kernel '" + kernel.id + "' must declare requirements",
                  form.location);
    if (kernel.preconditions.empty())
        ctx.error("optilisp.kernel.precondition",
                  "kernel '" + kernel.id + "' must declare at least one precondition",
                  form.location);
    if (kernel.forbids.empty())
        ctx.error("optilisp.kernel.forbids",
                  "kernel '" + kernel.id +
                      "' must declare at least one forbidden condition; missing information must "
                      "never authorize a rewrite",
                  form.location);
    if (kernel.correctness.empty())
        ctx.error("optilisp.kernel.correctness",
                  "kernel '" + kernel.id + "' must declare correctness obligations", form.location);
    if (kernel.expected_effects.empty())
        ctx.error("optilisp.kernel.effects",
                  "kernel '" + kernel.id + "' must declare expected effects", form.location);
    if (kernel.limitations.empty())
        ctx.error("optilisp.kernel.limitations",
                  "kernel '" + kernel.id + "' must declare limitations", form.location);
    if (kernel.documentation.empty())
        ctx.error("optilisp.kernel.documentation",
                  "kernel '" + kernel.id + "' must declare first-class documentation",
                  form.location);
    return ok;
}

bool parse_capability(const Expr& form, Registry& registry, const LoaderContext& ctx) {
    std::string id, name;
    std::set<Stage> stages;
    std::size_t i = 1;
    bool seen_stages = false;
    for (; i < form.items.size(); ++i) {
        const Expr& item = form.items[i];
        if (item.kind != ExprKind::keyword) break;
        if (item.kind == ExprKind::keyword && i + 1 >= form.items.size()) {
            ctx.error("optilisp.registry.field", "keyword '" + item.value + "' requires a value",
                      item.location);
            return false;
        }
        const Expr& value = form.items[i + 1];
        if (item.value == "id" && value.kind == ExprKind::string) id = value.value;
        else if (item.value == "name" && value.kind == ExprKind::string) name = value.value;
        else if (item.value == "stages" && value.kind == ExprKind::list) {
            seen_stages = true;
            for (const Expr& stage : value.items) {
                Stage parsed{};
                if (stage.kind != ExprKind::symbol || !stage_from(stage.value, parsed)) {
                    ctx.error("optilisp.registry.stage", "capability stage must be hir, mir, or lir",
                              stage.location);
                    return false;
                }
                stages.insert(parsed);
            }
        }
        i += 1;
    }
    if (id.empty()) {
        ctx.error("optilisp.registry.field", "capability is missing a stable :id", form.location);
        return false;
    }
    if (!seen_stages || stages.empty()) {
        ctx.error("optilisp.registry.stage", "capability '" + id + "' requires nonempty :stages",
                  form.location);
        return false;
    }
    if (registry.capabilities.count(id)) {
        ctx.error("optilisp.registry.duplicate", "duplicate capability identifier '" + id + "'",
                  form.location);
        return false;
    }
    registry.capabilities.emplace(id, std::move(stages));
    return true;
}

bool parse_analysis(const Expr& form, Registry& registry, const LoaderContext& ctx) {
    std::string id;
    std::size_t i = 1;
    for (; i < form.items.size(); ++i) {
        const Expr& item = form.items[i];
        if (item.kind != ExprKind::keyword) break;
        if (i + 1 >= form.items.size()) {
            ctx.error("optilisp.registry.field", "keyword '" + item.value + "' requires a value",
                      item.location);
            return false;
        }
        if (item.value == "id" && form.items[i + 1].kind == ExprKind::string)
            id = form.items[i + 1].value;
        i += 1;
    }
    if (id.empty()) {
        ctx.error("optilisp.registry.field", "analysis is missing a stable :id", form.location);
        return false;
    }
    if (registry.analyses.count(id)) {
        ctx.error("optilisp.registry.duplicate", "duplicate analysis identifier '" + id + "'",
                  form.location);
        return false;
    }
    registry.analyses.insert(id);
    return true;
}

// Monoids are syntactically read and validated for shape only; the loader
// never executes an operation, so no monoid law is claimed to hold here.
bool parse_monoid(const Expr& form, Diagnostics& diagnostics) {
    std::set<std::string> seen;
    std::size_t i = 1;
    bool has_laws = false;
    for (; i < form.items.size(); ++i) {
        const Expr& item = form.items[i];
        if (item.kind != ExprKind::keyword) break;
        if (i + 1 >= form.items.size()) {
            diagnostics.push_back({"optilisp.monoid.field",
                                   "keyword '" + item.value + "' requires a value", item.location});
            return false;
        }
        if (!seen.insert(item.value).second) {
            diagnostics.push_back({"optilisp.monoid.field", "duplicate keyword '" + item.value + "'",
                                   item.location});
            return false;
        }
        if (item.value == "id" && form.items[i + 1].kind != ExprKind::string) {
            diagnostics.push_back({"optilisp.monoid.field", ":id must be a string", item.location});
            return false;
        }
        i += 1;
    }
    for (; i < form.items.size(); ++i) {
        if (head_is(form.items[i], "laws")) {
            if (!seen.insert("laws").second) {
                diagnostics.push_back(
                    {"optilisp.monoid.field", "duplicate laws section", form.items[i].location});
                return false;
            }
            has_laws = true;
        }
    }
    if (!seen.contains("id")) {
        diagnostics.push_back(
            {"optilisp.monoid.field", "monoid is missing a stable :id", form.location});
        return false;
    }
    if (!has_laws) {
        diagnostics.push_back(
            {"optilisp.monoid.field", "monoid must declare its algebraic laws", form.location});
        return false;
    }
    return true;
}

void dispatch(const Expr& form, Registry* registry, std::vector<Kernel>* kernels,
              const LoaderContext& ctx) {
    const auto head = head_name(form);
    if (head == "define-kernel") {
        Kernel kernel;
        if (parse_kernel(form, kernel, ctx) && kernels) kernels->push_back(std::move(kernel));
    } else if (head == "define-capability") {
        if (registry && !parse_capability(form, *registry, ctx)) return;
    } else if (head == "define-analysis") {
        if (registry && !parse_analysis(form, *registry, ctx)) return;
    } else if (head == "define-monoid") {
        if (registry) parse_monoid(form, *ctx.diagnostics);
    } else {
        ctx.error("optilisp.form",
                  "unknown top-level form '" + std::string(head) +
                      "' (expected define-kernel, define-capability, define-analysis, or "
                      "define-monoid)",
                  form.location);
    }
}

std::vector<std::filesystem::path> olisp_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::is_directory(directory)) return files;
    // Deterministic traversal: recursive, then lexicographic by path.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".olisp") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

std::string_view spelling(Stage stage) {
    switch (stage) {
    case Stage::hir: return "hir";
    case Stage::mir: return "mir";
    case Stage::lir: return "lir";
    }
    return "hir";
}

Fact Facts::get(std::string_view key) const {
    const auto it = facts_.find(key);
    return it == facts_.end() ? Fact::unknown : it->second;
}

void Facts::set(std::string key, Fact value) { facts_[std::move(key)] = value; }

void Facts::invalidate(const AnalysisContract& contract) {
    // Conservative: an analysis that is not explicitly preserved becomes
    // invalidated, because this loader cannot prove a narrower claim.
    for (auto& [key, value] : facts_) {
        if (key.rfind("analysis:", 0) != 0) continue;
        const auto name = key.substr(9);
        if (contract.preserves.contains(name)) continue;
        value = Fact::invalidated;
    }
    for (const auto& name : contract.invalidates) facts_["analysis:" + name] = Fact::invalidated;
}

RegistryResult load_registry(const std::filesystem::path& directory) {
    RegistryResult result;
    if (!std::filesystem::is_directory(directory)) {
        result.diagnostics.push_back({"optilisp.registry.missing",
                                      "registry directory not found: " + directory.string(),
                                      {directory.string(), 1, 1}});
        return result;
    }
    LoaderContext ctx{&result.registry, Loader::Profile::blackstone, {}, &result.diagnostics};
    for (const auto& path : olisp_files(directory)) {
        const auto parsed = parse_file(path.string());
        for (auto& diagnostic : parsed.diagnostics) result.diagnostics.push_back(std::move(diagnostic));
        if (!parsed.ok()) continue;
        for (const Expr& form : parsed.forms) dispatch(form, &result.registry, nullptr, ctx);
    }
    return result;
}

Loader::Loader(Registry registry, ParseLimits limits, Profile profile)
    : registry_(std::move(registry)), limits_(limits), profile_(profile) {}

LoadResult Loader::load_source(std::string_view source, std::string file) const {
    LoadResult result;
    const auto parsed = parse(source, file, limits_);
    result.diagnostics = parsed.diagnostics;
    if (!parsed.ok()) return result;
    LoaderContext ctx{&registry_, profile_, limits_, &result.diagnostics};
    std::vector<Kernel> kernels;
    for (const Expr& form : parsed.forms) dispatch(form, nullptr, &kernels, ctx);
    // A failed parse or validation yields no kernels: never install a partial load.
    if (!result.diagnostics.empty()) return result;
    std::sort(kernels.begin(), kernels.end(),
              [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
    for (std::size_t i = 1; i < kernels.size(); ++i) {
        if (kernels[i].id == kernels[i - 1].id) {
            result.diagnostics.push_back({"optilisp.kernel.duplicate",
                                          "duplicate kernel identifier '" + kernels[i].id + "'",
                                          kernels[i].location});
            return {};
        }
    }
    result.kernels = std::move(kernels);
    return result;
}

LoadResult Loader::load(std::span<const std::filesystem::path> files) const {
    LoadResult result;
    std::vector<Kernel> all;
    for (const auto& path : files) {
        const auto parsed = parse_file(path.string(), limits_);
        for (auto& diagnostic : parsed.diagnostics) result.diagnostics.push_back(std::move(diagnostic));
        if (!parsed.ok()) continue;
        LoaderContext ctx{&registry_, profile_, limits_, &result.diagnostics};
        std::vector<Kernel> kernels;
        for (const Expr& form : parsed.forms) dispatch(form, nullptr, &kernels, ctx);
        all.insert(all.end(), std::make_move_iterator(kernels.begin()),
                   std::make_move_iterator(kernels.end()));
    }
    if (!result.diagnostics.empty()) return result;
    std::sort(all.begin(), all.end(), [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
    for (std::size_t i = 1; i < all.size(); ++i) {
        if (all[i].id == all[i - 1].id) {
            result.diagnostics.push_back(
                {"optilisp.kernel.duplicate", "duplicate kernel identifier '" + all[i].id + "'",
                 all[i].location});
            return {};
        }
    }
    result.kernels = std::move(all);
    return result;
}

LoadResult Loader::load_directory(const std::filesystem::path& directory) const {
    const auto files = olisp_files(directory);
    if (files.empty()) {
        LoadResult result;
        result.diagnostics.push_back(
            {"optilisp.kernel.missing", "no .olisp kernel files under " + directory.string(),
             {directory.string(), 1, 1}});
        return result;
    }
    return load(files);
}

bool Loader::install(const LoadResult& loaded, KernelConsumer& consumer,
                     Diagnostics& diagnostics) const {
    if (!loaded.ok()) {
        for (const auto& diagnostic : loaded.diagnostics) diagnostics.push_back(diagnostic);
        return false;
    }
    std::unique_ptr<Installation> batch = consumer.begin_installation(diagnostics);
    if (!batch) {
        diagnostics.push_back({"optilisp.install", "consumer did not provide an installation",
                               {}});
        return false;
    }
    for (const auto& kernel : loaded.kernels) {
        if (!batch->add(kernel, diagnostics)) return false; // batch destruction rolls back
    }
    return batch->commit(diagnostics);
}

} // namespace mlvm::optilisp