// Tests for the Optilisp reader, loader, rewrite engine, and MLVM pass.
//
// Layer 1 (syntax) is separated from layers 2-4 (structure, references,
// semantics) in the assertions: a form that reads is not automatically a
// valid kernel, and a syntactically invalid file produces no forms at all.

#include "test.hpp"
#include <mlvm/core.hpp>
#include <mlvm/ir.hpp>
#include <mlvm/optilisp/loader.hpp>
#include <mlvm/optilisp/pass.hpp>
#include <mlvm/optilisp/rewrite.hpp>
#include <fstream>
#include <sstream>
#include <string>

using namespace mlvm;
using namespace mlvm::optilisp;

namespace {

#ifndef MLVM_TEST_BLACKSTONE_DIR
#define MLVM_TEST_BLACKSTONE_DIR ""
#endif

// The checked-in Blackstone database supplies the stdlib registry and the
// kernel catalog; these paths come from CMake so the test never guesses.
std::filesystem::path blackstone_root() { return MLVM_TEST_BLACKSTONE_DIR; }
std::filesystem::path registry_directory() { return blackstone_root() / "optilisp" / "stdlib"; }
std::filesystem::path kernel_directory() { return blackstone_root() / "kernels"; }

const char* registry_source = R"(
(define-capability :id "typed-expressions" :name "Typed" :stages (hir mir)
  (description "Expressions carry explicit types."))
(define-capability :id "bitwise-semantics" :name "Bitwise" :stages (hir)
  (description "Bitwise operators are defined for the width."))
(define-capability :id "integer-overflow-semantics" :name "Overflow" :stages (hir)
  (description "Integer overflow behavior is defined."))
(define-capability :id "lir-only" :name "LIR only" :stages (lir)
  (description "Only available at the low level."))
(define-analysis :id "constant-value-analysis" :name "Constants"
  (description "Computes known constant values."))
(define-analysis :id "alias-analysis" :name "Alias"
  (description "Computes may-alias facts."))
(define-monoid :id "blackstone.monoid.integer-addition" :name "Integer Addition"
  :carrier integer :operation integer-add :identity 0
  (laws (associative integer-add) (left-identity integer-add 0))
  (description "Modular integer addition."))
)";

// A complete, valid HIR kernel.
const char* valid_kernel = R"(
(define-kernel
  :id "blackstone.hir.custom.and-self"
  :name "Custom bitwise identity"
  :stage hir
  (category algebraic-simplification)
  (paradigms algebraic-rewriting)
  (objective "Forward the operand of a repeated-operand integer AND.")
  (requires (capability typed-expressions) (capability integer-overflow-semantics))
  (preconditions (operand-has-compatible-integer-type))
  (forbids (operation-has-observable-side-effects))
  (transform (rewrite (bitwise-and x x) x))
  (correctness (preserve-observable-behavior))
  (expected-effects "Remove the redundant AND.")
  (limitations "Requires two uses of the same integer SSA definition.")
  (documentation (purpose "Demonstrate authoring an executable kernel.")))
)";

// A temporary directory holding the sample registry, so the loader is
// exercised against real files rather than an in-memory shortcut.
struct TempTree {
    std::filesystem::path root;
    explicit TempTree(const std::string& name) {
        root = std::filesystem::temp_directory_path() / ("optilisp-test-" + name);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "capabilities");
        std::filesystem::create_directories(root / "analyses");
        write(root / "capabilities" / "core.olisp", registry_source);
        write(root / "analyses" / "core.olisp", "");
    }
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    static void write(const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
};

bool has_diagnostic(const Diagnostics& diagnostics, std::string_view code) {
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.code == code) return true;
    return false;
}

bool mentions(const Diagnostics& diagnostics, std::string_view text) {
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.message.find(text) != std::string::npos) return true;
    return false;
}

// -------------------------------------------------------------------------
// Layer 1: syntax
// -------------------------------------------------------------------------

void test_reader_accepts_the_surface_grammar() {
    const auto source = R"(
; leading comment
(define-kernel :id "a" :stage hir (rewrite (add x y) y))
(symbol-with.dots 42 -7 3.14 :keyword true false nil "string \"escaped\"\n")
)";
    const auto parsed = parse(source, "sample.olisp");
    CHECK(parsed.ok());
    CHECK(parsed.forms.size() == 2);
    CHECK(parsed.forms[0].kind == ExprKind::list);
    CHECK(parsed.forms[0].items[1].kind == ExprKind::keyword);
    CHECK(parsed.forms[0].items[1].value == "id");
    CHECK(parsed.forms[0].items[2].kind == ExprKind::string);
    CHECK(parsed.forms[0].items[2].value == "a");

    const Expr& second = parsed.forms[1];
    CHECK(second.items[0].symbol("symbol-with.dots"));
    CHECK(second.items[1].kind == ExprKind::number && second.items[1].value == "42");
    CHECK(second.items[2].kind == ExprKind::number && second.items[2].value == "-7");
    CHECK(second.items[3].kind == ExprKind::number && second.items[3].value == "3.14");
    CHECK(second.items[4].kind == ExprKind::keyword && second.items[4].value == "keyword");
    CHECK(second.items[5].kind == ExprKind::boolean && second.items[5].value == "true");
    CHECK(second.items[6].kind == ExprKind::boolean && second.items[6].value == "false");
    CHECK(second.items[7].kind == ExprKind::nil);
    CHECK(second.items[8].kind == ExprKind::string);
    CHECK(second.items[8].value.find("\"escaped\"") != std::string::npos);

    // Locations are 1-based and point at the opening parenthesis.
    CHECK(parsed.forms[0].location.line == 3);
    CHECK(parsed.forms[0].location.column == 1);
    CHECK(parsed.forms[0].location.file == "sample.olisp");
}

void test_reader_rejects_malformed_source() {
    struct Case {
        const char* text;
        std::string_view code;
        std::string_view fragment;
    };
    const Case cases[] = {
        {"(a b", "optilisp.syntax", "unterminated list"},
        {"a)", "optilisp.syntax", "unexpected closing parenthesis"},
        {"(a \"unterminated)", "optilisp.syntax", "string"},
        {"(a \"bad \\q escape\")", "optilisp.syntax", "invalid escape"},
        {"(a :)", "optilisp.syntax", "keyword"},
    };
    for (const auto& c : cases) {
        const auto parsed = parse(c.text, "bad.olisp");
        CHECK(!parsed.ok());
        // A syntax error discards every form: no partial results escape.
        CHECK(parsed.forms.empty());
        CHECK(has_diagnostic(parsed.diagnostics, c.code));
        CHECK(mentions(parsed.diagnostics, c.fragment));
    }
}

void test_reader_enforces_limits() {
    const std::string deep = std::string(200, '(') + "x" + std::string(200, ')');
    const auto nested = parse(deep, "deep.olisp");
    CHECK(!nested.ok());
    CHECK(has_diagnostic(nested.diagnostics, "optilisp.limit"));
    CHECK(mentions(nested.diagnostics, "nesting"));

    ParseLimits tiny;
    tiny.max_bytes = 8;
    const auto oversized = parse("(this is far too long)", "big.olisp", tiny);
    CHECK(!oversized.ok());
    CHECK(mentions(oversized.diagnostics, "exceeds 1 MiB"));

    ParseLimits nodes;
    nodes.max_nodes = 2;
    const auto many = parse("(a b c d)", "many.olisp", nodes);
    CHECK(!many.ok());
    CHECK(mentions(many.diagnostics, "node limit"));
}

void test_reader_round_trips_print() {
    const auto parsed = parse("(add x (integer 3) :tag \"s\")", "rt.olisp");
    CHECK(parsed.ok());
    const std::string printed = parsed.forms[0].print();
    const auto again = parse(printed, "rt2.olisp");
    CHECK(again.ok());
    CHECK(again.forms[0] == parsed.forms[0]);
}

// -------------------------------------------------------------------------
// Layers 2-4: structure, references, semantics
// -------------------------------------------------------------------------

void test_registry_loading() {
    TempTree tree("registry");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    CHECK(registry.registry.capabilities.count("typed-expressions") == 1);
    CHECK(registry.registry.capabilities.count("integer-overflow-semantics") == 1);
    CHECK(registry.registry.analyses.count("constant-value-analysis") == 1);
    const auto& stages = registry.registry.capabilities.at("typed-expressions");
    CHECK(stages.count(Stage::hir) == 1 && stages.count(Stage::mir) == 1);
    CHECK(stages.count(Stage::lir) == 0);

    // Monoids are read for shape only; the loader never claims their laws hold.
    CHECK(registry.ok());
}

void test_registry_reports_missing_and_duplicate_identifiers() {
    TempTree tree("registry-dup");
    TempTree::write(tree.root / "analyses" / "dup.olisp",
                    "(define-analysis :id \"constant-value-analysis\" :name \"Again\")");
    const auto registry = load_registry(tree.root);
    CHECK(!registry.ok());
    CHECK(mentions(registry.diagnostics, "duplicate analysis"));

    const auto missing = load_registry(tree.root / "capabilities");
    CHECK(!missing.ok());
    CHECK(mentions(missing.diagnostics, "registry directory not found"));
}

void test_valid_kernel_loads() {
    TempTree tree("valid");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(valid_kernel, "valid.olisp");
    CHECK(loaded.ok());
    CHECK(loaded.kernels.size() == 1);
    const Kernel& kernel = loaded.kernels.front();
    CHECK(kernel.id == "blackstone.hir.custom.and-self");
    CHECK(kernel.stage == Stage::hir);
    CHECK(kernel.category == "algebraic-simplification");
    CHECK(kernel.paradigms.size() == 1 && kernel.paradigms[0] == "algebraic-rewriting");
    CHECK(kernel.requirements.size() == 2);
    CHECK(kernel.requirements[0].kind == "capability");
    CHECK(kernel.requirements[0].id == "typed-expressions");
    CHECK(kernel.preconditions.size() == 1 && kernel.forbids.size() == 1);
    CHECK(kernel.correctness.size() == 1);
    CHECK(kernel.documentation.count("purpose") == 1);
    CHECK(kernel.location.file == "valid.olisp");
    CHECK(kernel.transformation.kind == ExprKind::list);
    CHECK(kernel.transformation.items[0].symbol("rewrite"));
}

void test_structural_rejections() {
    TempTree tree("structure");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    const Loader loader(registry.registry);

    const char* missing_stage = R"(
(define-kernel :id "a.b.c" :name "No Stage"
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    CHECK(!loader.load_source(missing_stage, "s.olisp").ok());

    const char* unknown_section = R"(
(define-kernel :id "a.b.c" :name "Extra" :stage hir
  (category dead-code-elimination) (bonus "unsupported")
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    auto rejected = loader.load_source(unknown_section, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "unknown section"));

    const char* duplicate_section = R"(
(define-kernel :id "a.b.c" :name "Dupe" :stage hir
  (category dead-code-elimination) (category loop-optimization)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(duplicate_section, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "duplicate 'category' section"));

    const char* duplicate_keyword = R"(
(define-kernel :id "a.b.c" :id "d.e.f" :name "Dupe" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(duplicate_keyword, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "duplicate keyword"));

    const char* trailing_atom = R"(
(define-kernel :id "a.b.c" :name "Trailing" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p")) stray)";
    rejected = loader.load_source(trailing_atom, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "unexpected item"));

    const char* late_keyword = R"(
(define-kernel :id "a.b.c" :name "Late" :stage hir
  (category dead-code-elimination) :sneaky 1
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p")))";
    rejected = loader.load_source(late_keyword, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "appears after a body section"));

    const char* two_transforms = R"(
(define-kernel :id "a.b.c" :name "Two" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y) (rewrite (sub x y) y))
  (correctness (c)) (expected-effects "e") (limitations "l")
  (documentation (purpose "p")))";
    rejected = loader.load_source(two_transforms, "s.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "exactly one transformation"));

    // Unknown top-level forms are rejected by the semantic layer, not the reader.
    const char* unknown_form = "(define-nonsense :id \"x\")";
    const auto syntactically_valid = parse(unknown_form, "u.olisp");
    CHECK(syntactically_valid.ok()); // Layer 1 accepts it
    CHECK(!loader.load_source(unknown_form, "u.olisp").ok()); // Layer 2+ does not
}

void test_reference_and_stage_rejections() {
    TempTree tree("references");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    const Loader loader(registry.registry);

    const char* unknown_capability = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Unknown" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability no-such-capability)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    auto rejected = loader.load_source(unknown_capability, "r.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "unresolved capability"));

    const char* wrong_stage = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Wrong stage" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability lir-only)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(wrong_stage, "r.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "does not apply to stage"));

    const char* unknown_analysis = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Analysis" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (analysis ghost-analysis)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(unknown_analysis, "r.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "unresolved analysis"));

    const char* bad_verb = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Verb" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (teleport (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(bad_verb, "r.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "unknown transformation verb"));

    const char* wrong_arity = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Arity" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y))) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    rejected = loader.load_source(wrong_arity, "r.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "pattern and a result"));
}

void test_contradictory_contract_is_rejected() {
    TempTree tree("contradiction");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const char* source = R"(
(define-kernel :id "blackstone.hir.x.y" :name "Both" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (analysis constant-value-analysis))
  (analysis-contract (requires constant-value-analysis)
    (invalidates constant-value-analysis) (preserves constant-value-analysis))
  (preconditions (p)) (forbids (f)) (transform (rewrite (add x y) y))
  (correctness (c)) (expected-effects "e") (limitations "l")
  (documentation (purpose "p"))))";
    const auto rejected = loader.load_source(source, "c.olisp");
    CHECK(!rejected.ok());
    CHECK(mentions(rejected.diagnostics, "both invalidates and preserves"));
}

void test_blackstone_profile_tolerates_missing_contracts() {
    TempTree tree("profile");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    const char* legacy = R"(
(define-kernel :id "blackstone.hir.legacy.kernel" :name "Legacy" :stage hir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (eliminate (block :id dead))
  (correctness (c)) (expected-effects "e") (limitations "l")
  (documentation (purpose "p"))))";
    // The strict profile requires an explicit analysis contract.
    const Loader strict(registry.registry);
    CHECK(!strict.load_source(legacy, "legacy.olisp").ok());
    // The blackstone profile accepts the upstream catalog shape.
    const Loader lenient(registry.registry, {}, Loader::Profile::blackstone);
    const auto loaded = lenient.load_source(legacy, "legacy.olisp");
    CHECK(loaded.ok());
    CHECK(loaded.kernels.size() == 1);
}

void test_directory_loading_is_deterministic() {
    TempTree tree("directory");
    std::filesystem::create_directories(tree.root / "kernels" / "hir" / "algebraic");
    std::filesystem::create_directories(tree.root / "kernels" / "mir");
    TempTree::write(tree.root / "kernels" / "hir" / "algebraic" / "b.olisp", valid_kernel);
    TempTree::write(tree.root / "kernels" / "mir" / "a.olisp", R"(
(define-kernel :id "blackstone.mir.x.y" :name "Mir" :stage mir
  (category dead-code-elimination)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (rewrite (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))");
    const auto registry = load_registry(tree.root);
    CHECK(registry.ok());
    const Loader loader(registry.registry);
    const auto loaded = loader.load_directory(tree.root / "kernels");
    CHECK(loaded.ok());
    // Stable ID order regardless of filesystem traversal order.
    CHECK(loaded.kernels.size() == 2);
    CHECK(loaded.kernels[0].id == "blackstone.hir.custom.and-self");
    CHECK(loaded.kernels[1].id == "blackstone.mir.x.y");

    const auto empty = loader.load_directory(tree.root / "kernels" / "absent");
    CHECK(!empty.ok());
    CHECK(mentions(empty.diagnostics, "no .olisp kernel files"));
}

void test_duplicate_ids_are_rejected() {
    TempTree tree("duplicates");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(std::string(valid_kernel) + "\n" + valid_kernel,
                                           "twice.olisp");
    CHECK(!loaded.ok());
    CHECK(mentions(loaded.diagnostics, "duplicate kernel identifier"));
    CHECK(loaded.kernels.empty());
}

// -------------------------------------------------------------------------
// Facts
// -------------------------------------------------------------------------

void test_facts_are_five_valued_and_conservative() {
    Facts facts;
    CHECK(facts.get("analysis:alias-analysis") == Fact::unknown);
    facts.set("analysis:alias-analysis", Fact::proven_true);
    CHECK(facts.get("analysis:alias-analysis") == Fact::proven_true);

    AnalysisContract contract;
    contract.requires_analyses.insert("alias-analysis");
    contract.preserves.insert("preserved-analysis");
    contract.invalidates.insert("explicit-analysis");
    facts.set("analysis:preserved-analysis", Fact::proven_true);
    facts.set("capability:typed-expressions", Fact::proven_true);
    facts.invalidate(contract);
    // Consumed analyses go stale; explicitly preserved ones survive.
    CHECK(facts.get("analysis:alias-analysis") == Fact::invalidated);
    CHECK(facts.get("analysis:preserved-analysis") == Fact::proven_true);
    CHECK(facts.get("analysis:explicit-analysis") == Fact::invalidated);
    // Capabilities and history are not analysis results.
    CHECK(facts.get("capability:typed-expressions") == Fact::proven_true);
}

// -------------------------------------------------------------------------
// Rewrite engine
// -------------------------------------------------------------------------

RewriteContext permissive_context() {
    RewriteContext context;
    context.stage = Stage::hir;
    for (const char* key : {"capability:typed-expressions", "capability:integer-overflow-semantics"})
        context.facts.set(key, Fact::proven_true);
    context.prove = [](const ProofRequest&, std::string_view condition) {
        if (condition == "operation-has-observable-side-effects") return Fact::proven_false;
        return Fact::proven_true;
    };
    return context;
}

void test_rewrite_system_rewrites_matching_terms() {
    TempTree tree("rewrite");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(valid_kernel, "valid.olisp");
    CHECK(loaded.ok());

    RewriteSystem system;
    Diagnostics diagnostics;
    CHECK(loader.install(loaded, system, diagnostics));
    CHECK(system.kernels().size() == 1);

    const auto parsed = parse("(top (bitwise-and x x) (bitwise-and y (bitwise-and y y)))", "t");
    CHECK(parsed.ok());
    Expr term = parsed.forms[0];
    auto context = permissive_context();
    const auto result = system.optimize(term, context, 64);
    CHECK(result.success);
    CHECK(result.rewrites == 2);
    CHECK(term.print() == "(top x y)");
    CHECK(context.facts.get("history:blackstone.hir.custom.and-self") == Fact::proven_true);
}

void test_rewrite_system_requires_proofs() {
    TempTree tree("rewrite-proofs");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(valid_kernel, "valid.olisp");
    RewriteSystem system;
    Diagnostics diagnostics;
    CHECK(loader.install(loaded, system, diagnostics));

    const auto parsed = parse("(bitwise-and x x)", "t");
    Expr term = parsed.forms[0];

    // No capabilities: the kernel is inapplicable, not an error.
    RewriteContext empty;
    auto result = system.optimize(term, empty, 64);
    CHECK(result.success);
    CHECK(result.rewrites == 0);
    CHECK(term.print() == "(bitwise-and x x)");

    // Capabilities present but the precondition cannot be proven.
    RewriteContext unproven;
    unproven.facts.set("capability:typed-expressions", Fact::proven_true);
    unproven.facts.set("capability:integer-overflow-semantics", Fact::proven_true);
    unproven.prove = [](const ProofRequest&, std::string_view) { return Fact::unknown; };
    result = system.optimize(term, unproven, 64);
    CHECK(result.success);
    CHECK(result.rewrites == 0);
    CHECK(term.print() == "(bitwise-and x x)");

    // The forbidden condition must be proven false.
    RewriteContext not_proven_false;
    not_proven_false.facts = unproven.facts;
    not_proven_false.prove = [](const ProofRequest&, std::string_view) { return Fact::proven_true; };
    result = system.optimize(term, not_proven_false, 64);
    CHECK(result.success);
    CHECK(result.rewrites == 0);
}

void test_rewrite_system_rolls_back_on_failure() {
    TempTree tree("rewrite-rollback");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(valid_kernel, "valid.olisp");
    RewriteSystem system;
    Diagnostics diagnostics;
    CHECK(loader.install(loaded, system, diagnostics));

    // (bitwise-and x y) with x = y matches; budget 0 forces exhaustion.
    const auto parsed = parse("(f (bitwise-and x x) (bitwise-and a b) (bitwise-and c c))", "t");
    Expr term = parsed.forms[0];
    auto context = permissive_context();
    const auto result = system.optimize(term, context, 1);
    CHECK(!result.success);
    // Budget exhaustion discards every edit, including valid ones.
    CHECK(term.print() == parsed.forms[0].print());
    CHECK(!result.diagnostics.empty());
}

void test_rewrite_installation_is_transactional() {
    TempTree tree("rewrite-transaction");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const auto loaded = loader.load_source(valid_kernel, "valid.olisp");
    CHECK(loaded.ok());
    RewriteSystem system;

    // Abandoned batch: destruction must not publish anything.
    {
        Diagnostics scratch;
        auto pending = system.begin_installation(scratch);
        CHECK(pending != nullptr);
        CHECK(pending->add(loaded.kernels.front(), scratch));
    }
    CHECK(system.kernels().empty());

    // A rejected load is never handed to the consumer.
    const char* bad = "(define-kernel :id \"x\" :name \"y\" :stage hir)";
    const auto rejected_load = loader.load_source(bad, "bad.olisp");
    CHECK(!rejected_load.ok());
    Diagnostics rejected;
    CHECK(!loader.install(rejected_load, system, rejected));
    CHECK(system.kernels().empty());

    // A successful install publishes the whole batch at once.
    Diagnostics ok;
    CHECK(loader.install(loaded, system, ok));
    CHECK(system.kernels().size() == 1);

    // Installing the same kernel twice is a duplicate error, not a silent no-op.
    Diagnostics again;
    CHECK(!loader.install(loaded, system, again));
    CHECK(mentions(again, "already installed"));
    CHECK(system.kernels().size() == 1);
}

void test_rewrite_system_rejects_unexecutable_kernels() {
    TempTree tree("rewrite-unsupported");
    const auto registry = load_registry(tree.root);
    const Loader loader(registry.registry);
    const char* hoist = R"(
(define-kernel :id "blackstone.hir.x.hoist" :name "Hoist" :stage hir
  (category loop-optimization)
  (objective "o") (requires (capability typed-expressions)) (preconditions (p))
  (forbids (f)) (transform (hoist (add x y) y)) (correctness (c))
  (expected-effects "e") (limitations "l") (documentation (purpose "p"))))";
    const auto loaded = loader.load_source(hoist, "h.olisp");
    CHECK(loaded.ok());
    RewriteSystem system;
    Diagnostics diagnostics;
    // Installing a kernel this backend cannot execute must fail cleanly.
    CHECK(!loader.install(loaded, system, diagnostics));
    CHECK(mentions(diagnostics, "executable rewrite"));
    CHECK(system.kernels().empty());
}

// -------------------------------------------------------------------------
// MLVM integer backend end to end
// -------------------------------------------------------------------------

struct Fixture {
    Context context;
    SourceManager sources;
    DiagnosticEngine diagnostics;
    std::uint32_t file = 0;

    Fixture() {
        register_builtin_dialects(context, diagnostics);
        CHECK(!diagnostics.has_error());
    }
};

Module build(Fixture& fixture, const std::string& text) {
    fixture.file = fixture.sources.add("test.mlvm", text);
    auto module = parse_ir(fixture.context, fixture.sources, fixture.file, fixture.diagnostics);
    CHECK(module.has_value());
    return std::move(*module);
}

void test_integer_optimizer_forwards_operands() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  block() {
    %x:"i32" = "arith.constant"() ["value"="1"];
    %same:"i32" = "arith.and"(%x, %x) [];
    %use:"i32" = "arith.muli"(%same, %x) [];
  }
})");
    module.kernel_sources.push_back(valid_kernel);
    module.kernel_applications.push_back("blackstone.hir.custom.and-self");

    CHECK(run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(!fixture.diagnostics.has_error());
    // Applications are consumed on success; definitions remain.
    CHECK(module.kernel_applications.empty());
    CHECK(module.kernel_sources.size() == 1);
    // The AND is gone and its operand is forwarded to the multiplication.
    const std::string printed = print_ir(module);
    CHECK(printed.find("\"arith.and\"") == std::string::npos);
    CHECK(printed.find("arith.muli\"(%0, %0)") != std::string::npos);
    // The constant survives: removing the AND must not remove x.
    CHECK(printed.find("\"arith.constant\"") != std::string::npos);
}

void test_integer_optimizer_folds_constants() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  apply "blackstone.hir.constant-folding.integer-mul";
  block() {
    %a:"i32" = "arith.constant"() ["value"="6"];
    %b:"i32" = "arith.constant"() ["value"="7"];
    %answer:"i32" = "arith.muli"(%a, %b) [];
    "func.return"(%answer) [];
  }
})");
    CHECK(run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(!fixture.diagnostics.has_error());
    const std::string printed = print_ir(module);
    CHECK(printed.find("\"value\"=\"42\"") != std::string::npos);
    CHECK(printed.find("\"arith.muli\"") == std::string::npos);
}

void test_run_kernels_reports_unknown_applications() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  apply "blackstone.hir.nonexistent.kernel";
  block() { %x:"i32" = "arith.constant"() ["value"="1"]; }
})");
    CHECK(!run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(fixture.diagnostics.has_error());
}

void test_duplicate_application_is_rejected() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  apply "blackstone.hir.constant-folding.integer-mul";
  apply "blackstone.hir.constant-folding.integer-mul";
  block() {
    %a:"i32" = "arith.constant"() ["value"="6"];
    %b:"i32" = "arith.constant"() ["value"="7"];
    %answer:"i32" = "arith.muli"(%a, %b) [];
    "func.return"(%answer) [];
  }
})");
    CHECK(!run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(fixture.diagnostics.has_error());
}

void test_unsupported_kernel_fails_the_pass() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  apply "blackstone.mir.dead-code-elimination.unreachable-blocks";
  block() { %x:"i32" = "arith.constant"() ["value"="1"]; }
})");
    // A MIR kernel cannot run on an HIR module and block elimination is outside
    // the generic backend: the pass fails and the IR is left untouched.
    const std::string before = print_ir(module);
    CHECK(!run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(fixture.diagnostics.has_error());
    CHECK(print_ir(module).find("\"arith.constant\"") != std::string::npos);
    CHECK(print_ir(module) == before);
}

void test_run_kernels_with_no_applications_is_a_no_op() {
    Fixture fixture;
    Module module = build(fixture, R"(mlvm 1 {
  block() { %x:"i32" = "arith.constant"() ["value"="1"]; }
})");
    const std::string before = print_ir(module);
    CHECK(run_kernels(module, registry_directory(), kernel_directory(), fixture.diagnostics));
    CHECK(!fixture.diagnostics.has_error());
    CHECK(print_ir(module) == before);
}

} // namespace

int main() {
    return test_main([] {
        test_reader_accepts_the_surface_grammar();
        test_reader_rejects_malformed_source();
        test_reader_enforces_limits();
        test_reader_round_trips_print();
        test_registry_loading();
        test_registry_reports_missing_and_duplicate_identifiers();
        test_valid_kernel_loads();
        test_structural_rejections();
        test_reference_and_stage_rejections();
        test_contradictory_contract_is_rejected();
        test_blackstone_profile_tolerates_missing_contracts();
        test_directory_loading_is_deterministic();
        test_duplicate_ids_are_rejected();
        test_facts_are_five_valued_and_conservative();
        test_rewrite_system_rewrites_matching_terms();
        test_rewrite_system_requires_proofs();
        test_rewrite_system_rolls_back_on_failure();
        test_rewrite_installation_is_transactional();
        test_rewrite_system_rejects_unexecutable_kernels();
        test_integer_optimizer_forwards_operands();
        test_integer_optimizer_folds_constants();
        test_run_kernels_reports_unknown_applications();
        test_duplicate_application_is_rejected();
        test_unsupported_kernel_fails_the_pass();
        test_run_kernels_with_no_applications_is_a_no_op();
        std::cout << "optilisp: ok\n";
    });
}