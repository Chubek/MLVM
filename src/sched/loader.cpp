#include <mlvm/sched.hpp>
#include "catalog.hpp"
#include <Blackstone-Requires.h>
#include <dparse.h>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>

extern "C" D_ParserTables parser_tables_mlvm_blackstone;

namespace mlvm::sched {
namespace {
using Tree = D_ParseNode*;
std::string_view kind(Tree n) {
    const auto& symbol = parser_tables_mlvm_blackstone.symbols[n->symbol];
    return {symbol.name, static_cast<std::size_t>(symbol.name_len)};
}
std::vector<Tree> children(Tree n, unsigned depth = 0) {
    if (depth > BS_MAX_DEPTH) throw std::runtime_error("Blackstone syntax nesting exceeds 256");
    std::vector<Tree> result;
    for (int i = 0; i < d_get_number_of_children(n); ++i) {
        auto child = d_get_child(n, i);
        if (kind(child).find("__") != std::string_view::npos) {
            auto nested = children(child, depth + 1); result.insert(result.end(), nested.begin(), nested.end());
        } else result.push_back(child);
    }
    return result;
}
std::vector<Tree> named(Tree n, std::string_view name) {
    auto result = children(n);
    std::erase_if(result, [&](Tree child) { return kind(child) != name; }); return result;
}
Tree one(Tree n, std::string_view name) {
    auto result = named(n, name);
    if (result.size() != 1) throw std::runtime_error("invalid Blackstone syntax tree: expected " + std::string(name));
    return result[0];
}
std::string text(Tree n) { return {n->start_loc.s, n->end}; }
std::string identifier(Tree n) { return text(one(n, "identifier")); }
std::string integer(Tree n) {
    auto s = text(one(n, "digits"));
    for (auto c : children(n)) if (kind(c) == "-") s.insert(s.begin(), '-');
    return s;
}
std::uint32_t capture(Tree n) {
    auto s = text(one(n, "digits")); std::uint32_t v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !v)
        throw std::runtime_error("capture must be in 1..4294967295");
    return v;
}
std::string qualified(Tree n) {
    std::string s;
    for (auto id : named(n, "identifier")) { if (!s.empty()) s += "::"; s += text(id); }
    return s;
}
struct RuleStorage {
    std::vector<BSMatchNode> matches;
    std::vector<BSBuildNode> builds;
    std::deque<std::vector<std::uint32_t>> arguments;
    std::vector<BSGuard> guards;
};
struct KernelStorage {
    std::deque<std::string> strings;
    std::vector<BSDeclaration> bases, concerns, requires_;
    std::deque<RuleStorage> storage;
    std::vector<BSRule> rules;
    BSKernel kernel{};
    std::string path;
    const char* keep(std::string s) { strings.push_back(std::move(s)); return strings.back().c_str(); }
    BSSpan span(Tree n) { return {path.c_str(), static_cast<uint32_t>(n->start_loc.line), static_cast<uint32_t>(n->start_loc.col + 1)}; }
    std::uint32_t expression(Tree n, std::vector<BSBuildNode>& out, unsigned depth = 0) {
        if (depth > BS_MAX_DEPTH || out.size() >= BS_MAX_RULE_NODES) throw std::runtime_error("Blackstone expression is too large");
        auto k = kind(n);
        if (k == "capture") out.push_back({BS_CAPTURE, capture(n), 0, 0, "", span(n)});
        else if (k == "integer") out.push_back({BS_INTEGER, 0, 0, 0, keep(integer(n)), span(n)});
        else if (k == "match_atom" || k == "build_atom") {
            for (auto c : children(n)) if (kind(c) == "capture" || kind(c) == "integer" || kind(c) == "match" || kind(c) == "build")
                return expression(c, out, depth + 1);
            throw std::runtime_error("missing Blackstone expression atom");
        } else {
            std::vector<Tree> parts;
            for (auto c : children(n)) if (kind(c) == "match_atom" || kind(c) == "build_atom" || kind(c) == "operator") parts.push_back(c);
            if (parts.empty() || parts.size() % 2 != 1) throw std::runtime_error("invalid Blackstone expression");
            auto left = expression(parts[0], out, depth + 1);
            for (std::size_t i = 1; i < parts.size(); i += 2) {
                auto right = expression(parts[i + 1], out, depth + 1);
                out.push_back({BS_BUILD_BINARY, 0, left, right, keep(identifier(parts[i])), span(n)});
                left = static_cast<std::uint32_t>(out.size() - 1);
            }
            return left;
        }
        return static_cast<std::uint32_t>(out.size() - 1);
    }
    std::uint32_t pattern(Tree n, const std::vector<BSBuildNode>& expr, std::uint32_t index,
                          RuleStorage& out, unsigned depth = 0) {
        if (depth > BS_MAX_DEPTH || out.matches.size() >= BS_MAX_RULE_NODES) throw std::runtime_error("Blackstone pattern is too large");
        const auto& e = expr.at(index); BSMatchNode match{}; match.span = span(n); match.text = "";
        auto nested = named(n, "pattern");
        if (!nested.empty()) {
            if (nested.size() != 2 || e.kind != BS_BUILD_BINARY) throw std::runtime_error("signature/match tree mismatch: expected Binary");
            match.sort = BS_BINARY; match.text = e.text;
            match.left = pattern(nested[0], expr, e.left, out, depth + 1);
            match.right = pattern(nested[1], expr, e.right, out, depth + 1);
        } else {
            if (e.kind != BS_CAPTURE) throw std::runtime_error("signature/match tree mismatch: expected capture");
            match.sort = BS_TERM; match.capture = e.capture;
            auto ref = one(n, "refinement"); auto name = identifier(ref);
            const std::map<std::string, BSRefinement> refs{{"resolves", BS_RESOLVES}, {"literal", BS_LITERAL},
                {"nonliteral", BS_NONLITERAL}, {"exact", BS_EXACT}, {"same", BS_SAME}};
            if (!refs.contains(name)) throw std::runtime_error("unknown refinement '" + name + "'");
            match.refinement = refs.at(name);
            auto ints = named(ref, "integer"), caps = named(ref, "capture");
            if (name == "exact") { match.text = keep(integer(one(ref, "integer"))); if (!caps.empty()) throw std::runtime_error("exact requires an integer"); }
            else if (name == "same") { match.same = capture(one(ref, "capture")); if (!ints.empty()) throw std::runtime_error("same requires a capture"); }
            else if (!ints.empty() || !caps.empty()) throw std::runtime_error(name + " takes no arguments");
        }
        out.matches.push_back(match); return static_cast<std::uint32_t>(out.matches.size() - 1);
    }
    void guard(Tree n, RuleStorage& out, unsigned depth = 0) {
        if (depth > BS_MAX_DEPTH) throw std::runtime_error("Blackstone guard is too deeply nested");
        for (auto atom : named(n, "guard_atom")) {
            auto nested = named(atom, "guard");
            if (!nested.empty()) { guard(nested[0], out, depth + 1); continue; }
            out.arguments.emplace_back(); auto& args = out.arguments.back();
            for (auto list : named(atom, "arguments")) for (auto cap : named(list, "capture")) args.push_back(capture(cap));
            out.guards.push_back({keep(identifier(atom)), args.data(), args.size(), span(atom)});
        }
    }
    void lower(Tree tree) {
        auto header = one(tree, "kernel"); kernel.name = keep(identifier(header)); kernel.span = span(header);
        for (auto base : named(header, "inheritance")) for (auto q : named(base, "qualified")) bases.push_back({keep(qualified(q)), span(q)});
        for (auto decl : named(tree, "declaration")) {
            auto child = children(decl).at(0); auto name = qualified(one(child, "qualified"));
            auto& dest = kind(child) == "concern" ? concerns : requires_;
            auto groups = named(child, "group");
            if (groups.empty()) dest.push_back({keep(name), span(child)});
            else for (auto id : named(groups[0], "identifier")) dest.push_back({keep(name + "::" + text(id)), span(child)});
        }
        for (auto node : named(tree, "rule")) {
            auto ids = named(node, "identifier"); auto sort = text(ids.at(1));
            if (sort != "Term" && sort != "Binary") throw std::runtime_error("unknown result sort '" + sort + "'");
            storage.emplace_back(); auto& s = storage.back(); std::vector<BSBuildNode> match;
            auto root = expression(one(node, "match"), match);
            pattern(one(node, "pattern"), match, root, s);
            expression(one(node, "build"), s.builds); guard(one(node, "guard"), s);
            rules.push_back({keep(text(ids[0])), sort == "Term" ? BS_TERM : BS_BINARY,
                s.matches.data(), s.matches.size(), s.builds.data(), s.builds.size(), s.guards.data(), s.guards.size(), span(node)});
        }
        kernel.inherits = bases.data(); kernel.inherit_count = bases.size();
        kernel.concerns = concerns.data(); kernel.concern_count = concerns.size();
        kernel.requires_declarations = requires_.data(); kernel.require_count = requires_.size();
        kernel.rules = rules.data(); kernel.rule_count = rules.size();
    }
};
void advance(d_loc_t* loc) { if (*loc->s++ == '\n') { ++loc->line; loc->col = 0; } else ++loc->col; }
void whitespace(D_Parser* p, d_loc_t* loc, void**) {
    for (;;) {
        if (*loc->s && std::string_view(" \t\r\n").find(*loc->s) != std::string_view::npos) advance(loc);
        else if (loc->s[0] == '/' && loc->s[1] == '/') { while (*loc->s && *loc->s != '\n') advance(loc); }
        else if (loc->s[0] == '/' && loc->s[1] == '*') {
            advance(loc); advance(loc);
            while (*loc->s && !(loc->s[0] == '*' && loc->s[1] == '/')) advance(loc);
            if (!*loc->s) { ++p->syntax_errors; p->loc = *loc; return; }
            advance(loc); advance(loc);
        } else return;
    }
}
void parse_kernel(KernelStorage& out, const std::filesystem::path& file) {
    out.path = file.string(); std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open kernel '" + out.path + "'");
    std::string source{std::istreambuf_iterator<char>(input), {}};
    if (source.size() > 16 * 1024 * 1024 || source.find('\0') != std::string::npos) throw std::runtime_error("kernel exceeds 16 MiB or contains NUL");
    std::unique_ptr<D_Parser, decltype(&free_D_Parser)> p(new_D_Parser(&parser_tables_mlvm_blackstone, 0), free_D_Parser);
    if (!p) throw std::bad_alloc();
    p->save_parse_tree = 1; p->loc.line = 1; p->error_recovery = 0;
    p->initial_white_space_fn = whitespace; p->syntax_error_fn = [](D_Parser*) {};
    // Preflight comments and backticks so delimiters cannot be silently truncated.
    auto loc = p->loc; loc.s = source.data(); bool tick = false;
    while (*loc.s && !p->syntax_errors) { whitespace(p.get(), &loc, nullptr); if (*loc.s) { if (*loc.s == '`') tick = !tick; advance(&loc); } }
    Tree tree = !p->syntax_errors && !tick ? dparse(p.get(), source.data(), static_cast<int>(source.size())) : nullptr;
    auto release = [&] { if (tree) free_D_ParseNode(p.get(), tree); };
    if (!tree || p->syntax_errors) {
        release(); throw std::runtime_error(out.path + ":" + std::to_string(p->loc.line) + ":" + std::to_string(p->loc.col + 1) + ": invalid Blackstone syntax or unterminated delimiter");
    }
    try { out.lower(tree); } catch (const std::exception& e) { release(); throw std::runtime_error(out.path + ": " + e.what()); }
    release();
}
BSRegistry* registry(BSError& error) {
    auto* r = bs_registry_create(&error);
    if (!r) return nullptr;
    if (!detail::register_catalog(r, error)) { bs_registry_destroy(r); return nullptr; }
    const BSCapability values[]{{"Arithmetic::Values::AllOnes", "AllOnes", 1, ""}, {"Arithmetic::Values::Nonzero", "Nonzero", 1, ""}};
    const BSRequiresModule module{"MLVM.IntegerValues", values, 2};
    if (bs_requires_load(r, &module, &error) != BS_OK) { bs_registry_destroy(r); return nullptr; }
    return r;
}
}
Kernel::~Kernel() { bs_profile_destroy(profile_); bs_registry_destroy(registry_); }
std::shared_ptr<Kernel> Kernel::load(const std::filesystem::path& file,
    std::span<const std::filesystem::path> directories, DiagnosticEngine& d) {
    try {
        auto result = std::shared_ptr<Kernel>(new Kernel); BSError error{};
        result->registry_ = registry(error);
        if (!result->registry_) throw std::runtime_error(error.message);
        std::deque<KernelStorage> storage(1); parse_kernel(storage[0], file);
        std::set<std::filesystem::path> files{std::filesystem::canonical(file)};
        std::vector<std::filesystem::path> paths;
        for (const auto& dir : directories) {
            if (!std::filesystem::is_directory(dir)) throw std::runtime_error("kernel search directory does not exist: " + dir.string());
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
                if (entry.is_regular_file() && entry.path().extension() == ".bst" && files.insert(std::filesystem::canonical(entry.path())).second) paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) { storage.emplace_back(); parse_kernel(storage.back(), path); }
        std::vector<BSKernel> kernels;
        for (auto& item : storage) kernels.push_back(item.kernel);
        if (bs_profile_compile(result->registry_, kernels.data(), kernels.size(), kernels[0].name, &result->profile_, &error) != BS_OK)
            throw std::runtime_error(error.message);
        return result;
    } catch (const std::exception& e) { d.error("sched.kernel", e.what()); return {}; }
}
std::shared_ptr<Kernel> Kernel::read_profile(const std::filesystem::path& path, DiagnosticEngine& d) {
    auto result = std::shared_ptr<Kernel>(new Kernel); BSError error{};
    result->registry_ = registry(error);
    auto close = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(close)> file(std::fopen(path.string().c_str(), "rb"), close);
    if (!result->registry_ || !file || bs_profile_read(result->registry_, file.get(), &result->profile_, &error) != BS_OK) {
        d.error("sched.profile", file ? error.message : "cannot open profile: " + path.string()); return {};
    }
    return result;
}
bool Kernel::write_profile(const std::filesystem::path& path, DiagnosticEngine& d) const {
    BSError error{}; FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) { d.error("sched.profile", "cannot create profile: " + path.string()); return false; }
    auto status = bs_profile_write(profile_, file, &error); auto closed = std::fclose(file);
    if (status != BS_OK || closed) { d.error("sched.profile", closed ? "cannot finish profile write" : error.message); return false; }
    return true;
}
std::vector<std::string> Kernel::rules() const {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < bs_profile_rule_count(profile_); ++i) result.emplace_back(bs_profile_rule_name(profile_, i));
    return result;
}
std::optional<Graph> load(const std::filesystem::path& path, const Registry& registry, DiagnosticEngine& d) {
    if (path.extension() != ".sched") { d.error("sched.extension", "orchestrators require the .sched extension"); return {}; }
    std::ifstream input(path, std::ios::binary);
    if (!input) { d.error("sched.io", "cannot open orchestrator: " + path.string()); return {}; }
    std::string source{std::istreambuf_iterator<char>(input), {}};
    return parse(source, path.string(), registry, d);
}
} // namespace mlvm::sched
