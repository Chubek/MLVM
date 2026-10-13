#include <mlvm/sched.hpp>
#include <mlvm/dialect.hpp>
#include <mlvm/pass.hpp>
#include <mlvm/target.hpp>
#include <charconv>
#include <fstream>
#include <sstream>

namespace mlvm::sched {
namespace {
std::string option(const Options& o, std::string_view key, std::string fallback = {}) {
    auto it = o.find(key); return it == o.end() ? fallback : it->second;
}
void fields(const Options& o, std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, value] : o) {
        if (value.find('\0') != std::string::npos) throw std::invalid_argument("option contains NUL: " + key);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) throw std::invalid_argument("unknown action option '" + key + "'");
    }
}
std::string required(const Options& o, std::string_view name) {
    auto result = option(o, name); if (result.empty()) throw std::invalid_argument("missing action option '" + std::string(name) + "'"); return result;
}
const Artifact& one(std::span<const Artifact* const> inputs) {
    if (inputs.size() != 1) throw std::invalid_argument("action requires exactly one input");
    return *inputs[0];
}
std::size_t parse_size(std::string_view s) {
    std::size_t n = 0; auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) throw std::invalid_argument("expected a nonnegative integer: " + std::string(s));
    return n;
}
}
Registry Registry::builtins(Context& context) {
    Registry registry;
    registry.add("ir", [&context](const Options& o) {
        fields(o, {"path", "source"});
        if (o.contains("path") == o.contains("source")) throw std::invalid_argument("ir requires exactly one of path or source");
        return [&context, o](std::span<const Artifact* const> inputs, RunContext& run) -> std::optional<Artifact> {
            if (!inputs.empty()) throw std::invalid_argument("ir source takes no inputs");
            auto source = option(o, "source"), name = std::string("<sched-ir>");
            if (o.contains("path")) {
                auto path = run.base_directory / o.at("path"); name = path.string(); std::ifstream input(path);
                if (!input) throw std::runtime_error("cannot open IR: " + name);
                source.assign(std::istreambuf_iterator<char>(input), {});
                run.read_paths.insert(path);
            }
            SourceManager sources; auto file = sources.add(name, source);
            DiagnosticEngine local; auto module = parse_ir(context, sources, file, local);
            for (const auto& diagnostic : local.diagnostics()) run.diagnostics.report({diagnostic.severity, diagnostic.code, sources.format(diagnostic)});
            if (!module) return {};
            // SourceManager is local; retain useful diagnostics/provenance without dangling file IDs.
            walk(*module, [](Operation& op) { op.location.file_id = 0; });
            return Artifact{"mlvm", std::move(*module)};
        };
    });
    registry.add("kernel", [](const Options& o) {
        fields(o, {"path", "directory", "budget"}); auto path = required(o, "path"); auto budget = parse_size(option(o, "budget", "10000"));
        return [path, budget, directory = option(o, "directory")](std::span<const Artifact* const> inputs, RunContext& run) -> std::optional<Artifact> {
            auto input = one(inputs); std::vector<std::filesystem::path> directories;
            if (!directory.empty()) directories.push_back(run.base_directory / directory);
            auto file = run.base_directory / path;
            run.read_paths.insert(file);
            for (const auto& dir : directories) if (std::filesystem::is_directory(dir))
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
                    if (entry.is_regular_file() && entry.path().extension() == ".bst") run.read_paths.insert(entry.path());
            auto kernel = file.extension() == ".bsp" ? Kernel::read_profile(file, run.diagnostics) : Kernel::load(file, directories, run.diagnostics);
            if (!kernel) return {};
            bool ok = false;
            if (input.kind == "mlvm") ok = apply(*kernel, std::any_cast<Module&>(input.value), budget, run.events, run.diagnostics);
            else if (input.kind == "firestone.dataflow") ok = apply(*kernel, std::any_cast<Dataflow&>(input.value), budget, run.events, run.diagnostics);
            else throw std::invalid_argument("kernel requires mlvm or firestone.dataflow input");
            return ok ? std::optional<Artifact>(std::move(input)) : std::nullopt;
        };
    });
    registry.add("pass", [](const Options& o) {
        fields(o, {"name"}); auto name = required(o, "name");
        if (name != "constant-fold" && name != "canonicalize" && name != "dce") throw std::invalid_argument("unknown MLVM pass: " + name);
        return [name](std::span<const Artifact* const> inputs, RunContext& run) -> std::optional<Artifact> {
            auto artifact = one(inputs); auto& module = std::any_cast<Module&>(artifact.value);
            PassManager manager;
            manager.add(name == "constant-fold" ? constant_fold_pass() : name == "canonicalize" ? canonicalize_pass() : dead_code_elimination_pass());
            if (!manager.run(module, run.diagnostics)) return {};
            return artifact;
        };
    });
    registry.add("convert", [&context](const Options& o) {
        fields(o, {"to"}); auto to = required(o, "to");
        const std::set<std::string> supported{"mlvm", "firestone.dataflow", "firestone.3ac", "firestone.ssa", "high", "mid", "low", "machine", "interp", "llvmir", "wasm", "gpu"};
        if (!supported.contains(to)) throw std::invalid_argument("unknown conversion destination: " + to);
        return [&context, to](std::span<const Artifact* const> inputs, RunContext& run) -> std::optional<Artifact> {
            const auto& input = one(inputs);
            if (input.kind == to) return input;
            if (input.kind == "firestone.dataflow") {
                const auto& data = input.get<Dataflow>();
                if (to == "firestone.3ac") return Artifact{to, to_three_address(data)};
                if (to == "firestone.ssa") return Artifact{to, to_ssa(data)};
                if (to == "mlvm") { auto out = from_dataflow(data, context, run.diagnostics); if (out) return Artifact{"mlvm", std::move(*out)}; return {}; }
            }
            if (input.kind == "mlvm") {
                if (to == "firestone.dataflow") { auto out = to_dataflow(input.get<Module>(), run.diagnostics); if (out) return Artifact{to, std::move(*out)}; return {}; }
                auto module = input.get<Module>();
                if (!lower_dialect(module, to, run.diagnostics)) return {};
                return Artifact{"mlvm", std::move(module)};
            }
            throw std::invalid_argument("no conversion from " + input.kind + " to " + to);
        };
    });
    registry.add("verify", [](const Options& o) {
        fields(o, {});
        return [](std::span<const Artifact* const> inputs, RunContext&) -> std::optional<Artifact> { return one(inputs); };
    });
    registry.add("collect", [](const Options& o) {
        fields(o, {});
        return [](std::span<const Artifact* const> inputs, RunContext&) -> std::optional<Artifact> {
            if (inputs.empty()) throw std::invalid_argument("collect requires at least one input");
            std::vector<Artifact> result; for (auto* input : inputs) result.push_back(*input);
            return Artifact{"bundle", std::move(result)};
        };
    });
    registry.add("text", [](const Options& o) {
        fields(o, {});
        return [](std::span<const Artifact* const> inputs, RunContext&) -> std::optional<Artifact> { return Artifact{"text", print_artifact(one(inputs))}; };
    });
    registry.add("target", [](const Options& o) {
        fields(o, {"triple", "arguments"}); auto triple = required(o, "triple");
        auto targets = TargetRegistry::builtins(); if (!targets.find(triple)) throw std::invalid_argument("unknown target: " + triple);
        std::vector<std::uint64_t> args; std::istringstream list(option(o, "arguments")); std::string arg;
        while (std::getline(list, arg, ',')) args.push_back(parse_size(arg));
        if (!args.empty() && triple != "interp-unknown-unknown") throw std::invalid_argument("arguments option belongs to the interpreter target");
        return [triple, args](std::span<const Artifact* const> inputs, RunContext& run) -> std::optional<Artifact> {
            const auto& module = one(inputs).get<Module>();
            if (triple == "llvmir-unknown-unknown") { auto result = emit_llvm_ir(module, run.diagnostics); if (result) return Artifact{"text", std::move(*result)}; }
            else if (triple == "wasm32-unknown-unknown") { auto result = emit_wasm(module, run.diagnostics); if (result) return Artifact{"bytes", std::move(*result)}; }
            else if (triple == "x86_64-unknown-linux-gnu") {
                auto object = emit_x86_object(module, run.diagnostics);
                if (object) { auto bytes = write_elf(*object, run.diagnostics); if (bytes) return Artifact{"bytes", std::move(*bytes)}; }
            } else if (triple == "interp-unknown-unknown") {
                auto result = interpret(module, args, run.diagnostics); if (result) return Artifact{"text", std::to_string(*result) + "\n"};
            }
            return {};
        };
    });
    return registry;
}
std::string print_artifact(const Artifact& artifact) {
    if (artifact.kind == "mlvm") return print_ir(artifact.get<Module>());
    if (artifact.kind == "text") return artifact.get<std::string>();
    if (artifact.kind == "firestone.dataflow") {
        const auto& data = artifact.get<Dataflow>();
        return data.graph.graph().to_dot([](const firestone::ir::Instruction& i) {
            if (i.opcode == firestone::ir::Opcode::Constant) return i.operands[0].kind == firestone::ir::OperandKind::Symbol ? i.operands[0].symbol : std::to_string(i.operands[0].integer);
            return "opcode " + std::to_string(static_cast<int>(i.opcode));
        });
    }
    if (artifact.kind == "firestone.ssa") return artifact.get<firestone::ir::SimpleSSAGraph>().cfg().graph().to_dot([](const auto& b) { return b.name; });
    if (artifact.kind == "firestone.3ac") {
        std::ostringstream out;
        for (const auto& i : artifact.get<firestone::ir::Typed3AC>().instructions()) {
            if (i.result) out << '%' << *i.result << " = ";
            out << "op" << static_cast<int>(i.opcode);
            for (const auto& a : i.operands) { out << ' '; if (a.kind == firestone::ir::OperandKind::Value) out << '%' << a.value; else if (a.kind == firestone::ir::OperandKind::Integer) out << a.integer; else out << a.symbol; }
            out << '\n';
        }
        return out.str();
    }
    if (artifact.kind == "bundle") {
        std::string text;
        for (const auto& item : artifact.get<std::vector<Artifact>>()) text += "// " + item.kind + "\n" + print_artifact(item);
        return text;
    }
    throw std::invalid_argument("artifact has no textual renderer: " + artifact.kind);
}
} // namespace mlvm::sched
