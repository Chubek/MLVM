#include <cephyr/sched.hpp>
#include <cephyr/frontend.hpp>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace cephyr {
namespace {
std::string sched_option(const mlvm::sched::Options& o, std::string_view key, std::string fallback = {}) {
    auto it = o.find(key); return it == o.end() ? fallback : it->second;
}
void check_macro(const std::string& key) {
    if (key.empty() || !(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_') ||
        key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("invalid macro name: " + key);
}
std::vector<std::string> split_list(const mlvm::sched::Options& o, std::string_view key) {
    std::vector<std::string> result; std::string item;
    std::istringstream list(o.at(std::string(key)));
    while (std::getline(list, item, ',')) {
        if (item.empty() || item.find('\0') != std::string::npos)
            throw std::invalid_argument("option '" + std::string(key) + "' requires comma-separated nonempty values");
        result.push_back(item);
    }
    return result;
}
void report_new(mlvm::DiagnosticEngine& local, std::size_t& seen, mlvm::SourceManager& sources, mlvm::DiagnosticEngine& out) {
    for (std::size_t i = seen; i < local.diagnostics().size(); ++i) {
        const auto& diagnostic = local.diagnostics()[i];
        out.report({diagnostic.severity, diagnostic.code, sources.format(diagnostic)});
    }
    seen = local.diagnostics().size();
}
} // namespace

bool register_scheduler_actions(mlvm::sched::Registry& registry, mlvm::Context& context) {
    return registry.add("cephyr", [&context](const mlvm::sched::Options& o) {
        auto fields = [](const mlvm::sched::Options& options) {
            for (const auto& [key, value] : options) {
                if (value.find('\0') != std::string::npos) throw std::invalid_argument("option contains NUL: " + key);
                const std::set<std::string> allowed{"path", "source", "preprocessor", "system_preprocessor",
                    "sysroot", "includes", "defines"};
                if (!allowed.contains(key)) throw std::invalid_argument("unknown action option '" + key + "'");
            }
        };
        fields(o);
        if (o.contains("path") == o.contains("source")) throw std::invalid_argument("cephyr requires exactly one of path or source");
        const auto engine_name = sched_option(o, "preprocessor", "ucpp");
        if (engine_name != "internal" && engine_name != "ucpp" && engine_name != "system")
            throw std::invalid_argument("unknown preprocessor: " + engine_name + " (expected internal, ucpp, or system)");
        if (o.contains("includes")) split_list(o, "includes");
        if (o.contains("defines"))
            for (const auto& entry : split_list(o, "defines")) check_macro(entry.substr(0, entry.find('=')));
        return [&context, o, engine_name](std::span<const mlvm::sched::Artifact* const> inputs,
                                          mlvm::sched::RunContext& run) -> std::optional<mlvm::sched::Artifact> {
            if (!inputs.empty()) throw std::invalid_argument("cephyr source takes no inputs");
            PreprocessOptions preprocessor;
            preprocessor.engine = engine_name == "internal" ? Preprocessor::Internal
                : engine_name == "system"                  ? Preprocessor::System
                                                          : Preprocessor::Ucpp;
            preprocessor.system_command = sched_option(o, "system_preprocessor");
            if (o.contains("sysroot"))
                preprocessor.sysroot = o.at("sysroot").empty() ? std::filesystem::path{} : run.base_directory / o.at("sysroot");
            if (o.contains("includes"))
                for (const auto& dir : split_list(o, "includes")) preprocessor.include_paths.push_back(run.base_directory / dir);
            if (o.contains("defines"))
                for (const auto& entry : split_list(o, "defines")) {
                    const auto eq = entry.find('=');
                    preprocessor.defines[entry.substr(0, eq)] = eq == std::string::npos ? "1" : entry.substr(eq + 1);
                }
            auto code = sched_option(o, "source");
            auto name = std::string("<sched-cephyr>");
            if (o.contains("path")) {
                auto path = run.base_directory / o.at("path");
                name = path.string();
                std::ifstream input(path);
                if (!input) throw std::runtime_error("cannot open C source: " + name);
                code.assign(std::istreambuf_iterator<char>(input), {});
                run.read_paths.insert(path);
            }
            mlvm::SourceManager sources;
            auto file = sources.add(name, code);
            mlvm::DiagnosticEngine local; std::size_t seen = 0;
            auto expanded = preprocess(sources, file, preprocessor, local);
            report_new(local, seen, sources, run.diagnostics);
            if (!expanded) return {};
            auto lowered = lower(context, sources, sources.add(name + "<preprocessed>", *expanded), local);
            report_new(local, seen, sources, run.diagnostics);
            if (!lowered) return {};
            // SourceManager is local; retain useful diagnostics/provenance without dangling file IDs.
            mlvm::walk(*lowered, [](mlvm::Operation& op) { op.location.file_id = 0; });
            return mlvm::sched::Artifact{"mlvm", std::move(*lowered)};
        };
    });
}
} // namespace cephyr
