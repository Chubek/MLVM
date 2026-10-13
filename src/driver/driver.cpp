#include "driver.hpp"
#include "optimize.hpp"
#include "tool_process.hpp"
#include <mlvm/mlvm.hpp>
#include <mlvm/dialect.hpp>
#include <mlvm/target.hpp>
#include <mlvm/object.hpp>
#include <mlvm/codegen.hpp>
#include <mlvm/sched.hpp>
#ifdef MLVM_HAS_OPTILISP
#include <mlvm/optilisp/pass.hpp>
#endif
#ifdef MLVM_HAS_CEPHYR
#include <cephyr/frontend.hpp>
#include <cephyr/sched.hpp>
#endif
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace mlvm {
namespace {
struct Error : std::runtime_error {
    int status;
    Error(int status, std::string message) : std::runtime_error(std::move(message)), status(status) {}
};
std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error(3, "cannot open '" + path + "'");
    input.seekg(0, std::ios::end); auto length = input.tellg();
    if (length < 0 || length > 256 * 1024 * 1024) throw Error(3, "input is unreadable or exceeds 256 MiB: " + path);
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) throw Error(3, "cannot read '" + path + "'");
    return bytes;
}
std::string text(const std::vector<std::uint8_t>& bytes) { return {bytes.begin(), bytes.end()}; }
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec) && !ec) return true;
    return std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
}
void write(const std::string& path, std::span<const std::uint8_t> bytes, const std::vector<std::string>& inputs, bool executable = false) {
    if (path.empty()) throw Error(2, "output requires -o FILE");
    for (const auto& input : inputs) if (same_file(path, input)) throw Error(2, "output would overwrite input '" + input + "'");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error(3, "cannot open output '" + path + "'");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); output.close();
    if (!output) throw Error(3, "cannot write output '" + path + "'");
    if (executable) std::filesystem::permissions(path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
}
void write_text(const std::string& path, const std::string& value, const std::vector<std::string>& inputs) {
    if (path.empty() || path == "-") { std::cout << value; return; }
    write(path, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}, inputs);
}
void expand_response(std::string argument, std::vector<std::string>& args, std::set<std::string>& active,
                     std::vector<std::string>& inputs, unsigned depth = 0) {
    if (argument.empty() || argument[0] != '@') { args.push_back(std::move(argument)); return; }
    if (depth >= 16) throw Error(2, "response-file nesting exceeds 16");
    const auto path = argument.substr(1);
    inputs.push_back(path);
    if (!active.insert(std::filesystem::weakly_canonical(path).string()).second) throw Error(2, "response-file cycle: " + path);
    const auto data = text(read(path));
    std::string token; char quote = 0; bool escape = false, started = false;
    auto flush = [&] { if (started) expand_response(token, args, active, inputs, depth + 1); token.clear(); started = false; };
    for (char c : data) {
        if (escape) { token += c; escape = false; started = true; }
        else if (c == '\\') { escape = true; started = true; }
        else if (quote) { if (c == quote) quote = 0; else token += c; }
        else if (c == '\'' || c == '"') { quote = c; started = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) flush();
        else { token += c; started = true; }
        if (args.size() > 65536) throw Error(2, "too many response-file arguments");
    }
    if (quote || escape) throw Error(2, "unterminated quote or escape in response file");
    flush(); active.erase(std::filesystem::weakly_canonical(path).string());
}
void help(std::string_view name) {
    std::cout << name << " (MLVM " << version << ")\nUsage: " << name << " [options] FILE...\n"
              << "Common: --help --version --color=never @RESPONSE --\n";
    if (name == "mlvm") std::cout << "  --verify --print --passes=constant-fold,canonicalize,dce --dump-passes -o FILE\n"
        << "  --convert-to=high|mid|low|machine|interp|llvmir|wasm|gpu\n"
        << "  --target=x86_64-unknown-linux-gnu|wasm32-unknown-unknown|llvmir-unknown-unknown|interp-unknown-unknown\n"
        << "  --args=35 or --arguments=1,2 (interp target only) -o FILE\n"
        << "  --schedule=FILE.sched --result=NODE --trace (or pass a .sched file directly)\n"
        << "  --blackstone-dir=DIR --kernel-file=FILE --apply-kernel=ID (repeatable)\n";
    else if (name == "mlvm-sched") std::cout << "  FILE.sched --check --dot --trace --result=NODE -o FILE\n";
    else if (name == "mlvm-as") std::cout << "  --target=x86_64-unknown-linux-gnu -o FILE (required)\n";
    else if (name == "mlvm-ld") std::cout << "  -r (relocatable) -e SYMBOL (default _start) -o FILE (required)\n";
    else if (name == "mlvm-ar") std::cout << "  t ARCHIVE | q -o ARCHIVE FILE... (deterministic short-name archive)\n";
    else if (name == "mlvm-strings") std::cout << "  -n LENGTH (default 4), printable ASCII strings\n";
    else if (name == "mlvm-objcopy") std::cout << "  --remove-section=NAME --only-section=NAME --rename-section=OLD=NEW -O binary -o FILE\n";
    else if (name == "mlvm-strip") std::cout << "  -o FILE (remove unreferenced local symbols)\n";
    else if (name == "mlvm-objdump") std::cout << "  -d (documented x86 subset) -s (section bytes) -t (symbols) -r (relocations)\n";
    else if (name == "cephyr") std::cout << "  -E --parse-only -fsyntax-only -i/--emit-ir -s/-S -c -o FILE -I DIR -D NAME[=VALUE] -U NAME\n"
        << "  -include FILE -std=c11 -ffreestanding -target x86_64-unknown-linux-gnu -O0 -O1\n"
        << "  --target=llvmir-unknown-unknown|wasm32-unknown-unknown|interp-unknown-unknown\n"
        << "  --args=35 or --arguments=1,2 (interp target only)\n"
        << "  --preprocessor=internal|ucpp|system (default ucpp) --system-preprocessor=COMMAND\n"
        << "  --sysroot=DIR (default bundled libc) --no-default-sysroot -nostdinc\n"
        << "  Default: link an executable; accepts .c, .o, .a; -nostdlib -nodefaultlibs -nostartfiles\n"
        << "  --blackstone-dir=DIR --kernel-file=FILE --apply-kernel=ID (repeatable)\n"
        << "  --optimize FILE.json -fID -fFILE.olisp (repeatable, ordered)\n"
        << "  --assembler COMMAND --linker COMMAND (CEPHYR_AS/CEPHYR_LD; default mlvm)\n"
        << "  C11 slice: integers, pointers, arrays, globals, static storage; see docs/CEPHYR.md\n";
    else std::cout << "  Inspect x86-64 ELF64 relocatable objects (see docs/TOOLS.md).\n";
}
// Archive extraction is shared by mlvm-ld and Cephyr's runtime linker.
bool link_input(const std::string& path, std::vector<Object>& objects, DiagnosticEngine& diagnostics) {
    const auto bytes = read(path);
    if (bytes.size() < 8 || std::string_view(reinterpret_cast<const char*>(bytes.data()), 8) != "!<arch>\n") {
        auto object = read_elf(bytes, diagnostics);
        if (!object) return false;
        objects.push_back(std::move(*object));
        return true;
    }
    auto members = read_archive(bytes, diagnostics);
    if (!members) return false;
    std::vector<Object> candidates;
    for (const auto& member : *members) {
        auto object = read_elf(member.data, diagnostics);
        if (!object) return false;
        candidates.push_back(std::move(*object));
    }
    std::set<std::size_t> extracted;
    for (;;) {
        auto combined = link_relocatable(objects, diagnostics);
        if (!combined) return false;
        std::set<std::string> needed;
        for (const auto& symbol : combined->symbols)
            if (symbol.section == -1 && symbol.binding == Binding::global) needed.insert(symbol.name);
        bool changed = false;
        for (std::size_t i = 0; i < candidates.size(); ++i) if (!extracted.contains(i)) {
            for (const auto& symbol : candidates[i].symbols) {
                if (symbol.section != -1 && symbol.binding != Binding::local && needed.contains(symbol.name)) {
                    objects.push_back(candidates[i]); extracted.insert(i); changed = true; break;
                }
            }
            if (changed) break;
        }
        if (!changed) return true;
    }
}
struct Options {
    struct KernelRequest { std::string value; bool file = false; };
    std::vector<std::string> inputs;
    std::vector<std::string> protected_inputs;
    std::string output, entry = "_start", passes, remove, only, rename, mode, archive_command;
    std::string blackstone;
    std::string schedule, result;
    std::string convert_to, target, emit_target, target_args;
    bool trace = false, schedule_check = false, schedule_dot = false;
    std::vector<std::string> kernel_files;
    std::vector<KernelRequest> kernel_applications;
    bool relocatable = false, dump = false, raw = false, disassemble = false;
    std::size_t minimum = 4;
#ifdef MLVM_HAS_CEPHYR
    cephyr::PreprocessOptions preprocessor;
    std::string assembler = "mlvm", linker = "mlvm";
    bool no_startfiles = false, no_defaultlibs = false;
#endif
};
#ifdef MLVM_HAS_CEPHYR
void optimizer_plan(Options& opts, const std::string& path) {
    using driver_detail::PlanValue;
    opts.protected_inputs.push_back(path);
    const auto source = text(read(path));
    try {
        const auto plan = driver_detail::PlanReader(source).read();
        const auto* object = std::get_if<PlanValue::Object>(&plan.data);
        if (!object) throw std::runtime_error("expected an object with kernels and optional kernel_files");
        auto string = [](const PlanValue& value) -> const std::string& {
            const auto* s = std::get_if<std::string>(&value.data);
            if (!s || s->empty() || s->find('\0') != std::string::npos) throw std::runtime_error("expected nonempty string without NUL");
            return *s;
        };
        auto file = [&](const PlanValue& value) {
            auto resolved = (std::filesystem::path(path).parent_path() / string(value)).lexically_normal().string();
            opts.protected_inputs.push_back(resolved);
            return resolved;
        };
        if (!object->contains("kernels")) throw std::runtime_error("missing kernels array");
        for (const auto& [key, value] : *object) {
            if (key != "kernels" && key != "kernel_files") throw std::runtime_error("unknown field: " + key);
            const auto* array = std::get_if<PlanValue::Array>(&value.data);
            if (!array) throw std::runtime_error(key + " must be an array");
            for (const auto& item : *array) {
                if (key == "kernel_files") opts.kernel_files.push_back(file(item));
                else if (const auto* entry = std::get_if<PlanValue::Object>(&item.data)) {
                    if (entry->size() != 1 || !entry->contains("file")) throw std::runtime_error("kernel entry must be an ID or {\"file\":\"path.olisp\"}");
                    opts.kernel_applications.push_back({file(entry->at("file")), true});
                } else opts.kernel_applications.push_back({string(item), false});
            }
        }
    } catch (const std::runtime_error& error) { throw Error(2, path + ": " + error.what()); }
}
#endif
Options options(std::string_view name, const std::vector<std::string>& args) {
    Options result; bool positional = false;
#ifdef MLVM_HAS_CEPHYR
    if (name == "cephyr") {
        if (const auto* command = std::getenv("CEPHYR_AS"); command && *command) result.assembler = command;
        if (const auto* command = std::getenv("CEPHYR_LD"); command && *command) result.linker = command;
    }
#endif
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        auto next = [&]() { if (++i == args.size()) throw Error(2, "missing argument for " + arg); return args[i]; };
        if (!positional && arg == "--") { positional = true; continue; }
        if (!positional && arg == "--color=never") continue;
        if (!positional && arg == "-o") {
            if (name != "mlvm" && name != "mlvm-sched" && name != "cephyr" && name != "mlvm-as" && name != "mlvm-ld" && name != "mlvm-ar" && name != "mlvm-objcopy" && name != "mlvm-strip") throw Error(2, "-o is unsupported by this tool");
            result.output = next(); continue;
        }
        if (!positional && (arg.starts_with("--target=") || (name == "cephyr" && arg == "-target"))) {
            const auto target = arg == "-target" ? next() : arg.substr(9);
            if (name == "mlvm") {
                auto registry = TargetRegistry::builtins();
                if (!registry.find(target)) throw Error(2, "unsupported emission target: " + target);
                if (!result.target.empty() && result.target != target) throw Error(2, "conflicting --target options");
                result.target = target;
                continue;
            }
            if (name == "cephyr") {
                // Dialect-backend emission for convertible modules; the x86-64
                // triple keeps its existing assembler-selection meaning below.
                if (target == "llvmir-unknown-unknown" || target == "wasm32-unknown-unknown" ||
                    target == "interp-unknown-unknown") {
                    if (!result.emit_target.empty() && result.emit_target != target)
                        throw Error(2, "conflicting --target options");
                    result.emit_target = target;
                    continue;
                }
                auto registry = TargetRegistry::builtins(); auto* description = registry.find(target);
                if (!description || !description->assembler) throw Error(2, "unsupported cephyr target: " + target);
                continue;
            }
            if (name != "mlvm-as") throw Error(2, "target selection unsupported by this tool");
            auto registry = TargetRegistry::builtins(); auto* description = registry.find(target);
            if (!description || !description->assembler) throw Error(2, "unsupported assembler target: " + target);
            continue;
        }
        if (!positional && (name == "mlvm" || name == "cephyr")) {
            if (arg.starts_with("--kernel-file=")) {
                result.kernel_files.push_back(arg.substr(14)); result.protected_inputs.push_back(arg.substr(14)); continue;
            }
            if (arg.starts_with("--apply-kernel=")) { result.kernel_applications.push_back({arg.substr(15), false}); continue; }
            if (arg.starts_with("--blackstone-dir=")) { result.blackstone = arg.substr(17); continue; }
        }
        if (!positional && (name == "mlvm" || name == "mlvm-sched")) {
            if (arg.starts_with("--schedule=")) { result.schedule = arg.substr(11); continue; }
            if (arg.starts_with("--result=")) { result.result = arg.substr(9); continue; }
            if (arg == "--trace") { result.trace = true; continue; }
            if (arg == "--check") { result.schedule_check = true; continue; }
            if (arg == "--dot") { result.schedule_dot = true; continue; }
        }
        if (!positional && name == "mlvm") {
            if (arg == "--verify" || arg == "--print") { result.mode = arg; continue; }
            if (arg.starts_with("--passes=")) { result.passes = arg.substr(9); continue; }
            if (arg == "--dump-passes") { result.dump = true; continue; }
            if (arg.starts_with("--convert-to=")) {
                const auto dialect = arg.substr(13);
                const std::set<std::string> supported{"high", "mid", "low", "machine", "interp", "llvmir", "wasm", "gpu"};
                if (!supported.contains(dialect)) throw Error(2, "unsupported conversion dialect: " + dialect);
                if (!result.convert_to.empty() && result.convert_to != dialect) throw Error(2, "conflicting --convert-to options");
                result.convert_to = dialect;
                continue;
            }
        }
        if (!positional && (name == "mlvm" || name == "cephyr")) {
            if (arg.starts_with("--args=") || arg.starts_with("--arguments=")) {
                const auto eq = arg.find('=');
                const auto value = arg.substr(eq + 1);
                if (!result.target_args.empty()) throw Error(2, "conflicting interpreter argument options");
                result.target_args = value;
                continue;
            }
        }
        if (!positional && name == "mlvm-ld") {
            if (arg == "-r") { result.relocatable = true; continue; }
            if (arg == "-e" || arg == "--entry") { result.entry = next(); continue; }
        }
        if (!positional && name == "mlvm-strings" && arg == "-n") {
            const auto value = next(); const auto r = std::from_chars(value.data(), value.data() + value.size(), result.minimum);
            if (r.ec != std::errc{} || r.ptr != value.data() + value.size() || !result.minimum) throw Error(2, "string length must be positive");
            continue;
        }
        if (!positional && name == "mlvm-objcopy") {
            if (arg.starts_with("--remove-section=")) { result.remove = arg.substr(17); continue; }
            if (arg.starts_with("--only-section=")) { result.only = arg.substr(15); continue; }
            if (arg.starts_with("--rename-section=")) { result.rename = arg.substr(17); continue; }
            if (arg == "-O") { if (next() != "binary") throw Error(2, "only binary output conversion is supported"); result.raw = true; continue; }
        }
        if (!positional && name == "mlvm-objdump" && (arg == "-d" || arg == "-s" || arg == "-t" || arg == "-r")) {
            if (arg == "-d") result.disassemble = true;
            continue;
        }
#ifdef MLVM_HAS_CEPHYR
        if (!positional && name == "cephyr") {
            if (arg == "-E" || arg == "-fsyntax-only" || arg == "--parse-only" || arg == "--emit-ir" || arg == "-i" || arg == "-s" || arg == "-S" || arg == "-c") {
                const auto mode = arg == "-i" ? "--emit-ir" : arg == "-s" ? "-S" : arg;
                if (!result.mode.empty() && result.mode != mode) throw Error(2, "conflicting compilation modes");
                result.mode = mode; continue;
            }
            if (arg == "-std=c11" || arg == "-ffreestanding" || arg == "-O0" || arg == "-O1") continue;
            auto flag_value = [&](std::string_view prefix) -> std::optional<std::string> {
                if (arg == prefix) return next();
                if (arg.starts_with(prefix) && arg[prefix.size()] == '=') return arg.substr(prefix.size() + 1);
                return std::nullopt;
            };
            if (auto command = flag_value("--assembler")) {
                if (command->empty()) throw Error(2, "--assembler requires a command");
                result.assembler = *command; continue;
            }
            if (auto command = flag_value("--linker")) {
                if (command->empty()) throw Error(2, "--linker requires a command");
                result.linker = *command; continue;
            }
            if (auto plan = flag_value("--optimize")) { optimizer_plan(result, *plan); continue; }
            if (arg.starts_with("-f")) {
                auto value = arg.size() == 2 ? next() : arg.substr(2);
                if (value.empty()) throw Error(2, "-f requires a kernel ID or Optilisp file");
                const bool file = value.ends_with(".olisp") || value.find('/') != std::string::npos || std::filesystem::is_regular_file(value);
                if (file) result.protected_inputs.push_back(value);
                result.kernel_applications.push_back({std::move(value), file}); continue;
            }
            if (auto engine = flag_value("--preprocessor")) {
                if (*engine == "internal") result.preprocessor.engine = cephyr::Preprocessor::Internal;
                else if (*engine == "ucpp") result.preprocessor.engine = cephyr::Preprocessor::Ucpp;
                else if (*engine == "system") result.preprocessor.engine = cephyr::Preprocessor::System;
                else throw Error(2, "unknown preprocessor: " + *engine + " (expected internal, ucpp, or system)");
                continue;
            }
            if (auto command = flag_value("--system-preprocessor")) {
                if (command->empty()) throw Error(2, "--system-preprocessor requires a command");
                result.preprocessor.system_command = std::move(*command); continue;
            }
            if (arg == "-include") { result.preprocessor.forced_includes.push_back(next()); continue; }
            if (arg.starts_with("-I")) { result.preprocessor.include_paths.push_back(arg.size() == 2 ? next() : arg.substr(2)); continue; }
            if (arg == "-nostdinc") { result.preprocessor.standard_includes = false; continue; }
            if (auto root = flag_value("--sysroot")) {
                if (root->empty()) throw Error(2, "--sysroot requires a directory");
                result.preprocessor.sysroot = *root; continue;
            }
            if (arg == "--no-default-sysroot") { result.preprocessor.sysroot.clear(); continue; }
            if (arg == "-nostdlib") { result.no_startfiles = result.no_defaultlibs = true; continue; }
            if (arg == "-nostartfiles") { result.no_startfiles = true; continue; }
            if (arg == "-nodefaultlibs") { result.no_defaultlibs = true; continue; }
            if (arg.starts_with("-D") || arg.starts_with("-U")) {
                auto value = arg.size() == 2 ? next() : arg.substr(2); const auto eq = value.find('='); const auto key = value.substr(0, eq);
                if (key.empty() || !(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_') ||
                    key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) throw Error(2, "invalid macro name: " + key);
                if (arg[1] == 'U') { if (eq != std::string::npos) throw Error(2, "-U requires a macro name"); result.preprocessor.defines.erase(key); result.preprocessor.undefines.insert(key); }
                else { result.preprocessor.undefines.erase(key); result.preprocessor.defines[key] = eq == std::string::npos ? "1" : value.substr(eq + 1); }
                continue;
            }
        }
#endif
        if (!positional && name == "mlvm-ar" && result.archive_command.empty() && (arg == "t" || arg == "q")) { result.archive_command = arg; continue; }
        if (!positional && arg.starts_with('-')) throw Error(2, "unsupported option: " + arg);
        result.inputs.push_back(arg);
    }
    if (result.inputs.empty() && result.schedule.empty()) throw Error(2, "expected input file");
    if (name == "cephyr" && (!result.kernel_files.empty() || !result.kernel_applications.empty() || !result.blackstone.empty()) &&
        !result.mode.empty() && result.mode != "--emit-ir" && result.mode != "-S" && result.mode != "-c")
        throw Error(2, "Cephyr kernels require compilation or linking");
    if (name == "cephyr" && !result.emit_target.empty() && !result.mode.empty())
        throw Error(2, "--target emission cannot be combined with " + result.mode);
    return result;
}
void inspect(const Object& object, std::string_view tool, bool disassemble) {
    if (tool == "mlvm-size") {
        std::uint64_t total = 0; std::cout << "section\tsize\n";
        for (const auto& s : object.sections) { std::cout << s.name << '\t' << s.data.size() << '\n'; total += s.data.size(); }
        std::cout << "total\t" << total << '\n'; return;
    }
    if (tool != "mlvm-nm") {
        std::cout << "ELF64 little-endian x86-64 relocatable\n";
        for (std::size_t i = 0; i < object.sections.size(); ++i) {
            const auto& s = object.sections[i];
            std::cout << "section " << i << ' ' << s.name << " size=" << s.data.size() << " align=" << s.alignment << " flags=" << s.flags << '\n';
            if (tool == "mlvm-objdump") for (std::size_t p = 0; p < s.data.size();) {
                std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << p << ": ";
                const auto byte = s.data[p];
                if (disassemble && (s.flags & 4)) {
                    if (auto decoded = x86_64::decode(std::span(s.data).subspan(p))) {
                        std::cout << x86_64::format(decoded->instruction); p += decoded->size;
                    } else { std::cout << ".byte 0x" << std::setw(2) << unsigned(byte); ++p; }
                } else {
                    const auto end = std::min(p + 16, s.data.size());
                    for (; p < end; ++p) std::cout << std::setw(2) << unsigned(s.data[p]) << ' ';
                }
                std::cout << std::dec << std::setfill(' ') << '\n';
            }
        }
    }
    for (const auto& s : object.symbols) {
        const char binding = s.binding == Binding::local ? 'l' : s.binding == Binding::weak ? 'w' : 'g';
        std::cout << std::hex << std::setw(16) << std::setfill('0') << s.value << std::dec << std::setfill(' ') << ' ' << binding << ' '
                  << (s.section == -1 ? "UND" : s.section == -2 ? "ABS" : object.sections[static_cast<std::size_t>(s.section)].name) << ' ' << s.name << '\n';
    }
    if (tool != "mlvm-nm") for (const auto& r : object.relocations)
        std::cout << "reloc " << object.sections[r.section].name << '+' << r.offset << " type=" << r.type << ' ' << object.symbols[r.symbol].name << " addend=" << r.addend << '\n';
}
bool transform(Object& object, const Options& options, bool strip, DiagnosticEngine& diagnostics) {
    std::vector<std::int32_t> sections(object.sections.size(), -1);
    Object result;
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        auto section = object.sections[i];
        if ((!options.remove.empty() && section.name == options.remove) || (!options.only.empty() && section.name != options.only)) continue;
        if (!options.rename.empty()) {
            const auto eq = options.rename.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == options.rename.size()) throw Error(2, "expected --rename-section=OLD=NEW");
            if (section.name == options.rename.substr(0, eq)) section.name = options.rename.substr(eq + 1);
        }
        sections[i] = static_cast<std::int32_t>(result.sections.size()); result.sections.push_back(std::move(section));
    }
    std::set<std::size_t> referenced;
    for (const auto& r : object.relocations) if (sections[r.section] >= 0) referenced.insert(r.symbol);
    std::vector<std::size_t> symbols(object.symbols.size(), SIZE_MAX);
    for (std::size_t i = 0; i < object.symbols.size(); ++i) {
        auto symbol = object.symbols[i];
        if (symbol.section >= 0 && sections[static_cast<std::size_t>(symbol.section)] < 0) {
            if (referenced.contains(i)) { diagnostics.error("object.section", "retained relocation refers to removed section"); return false; }
            continue;
        }
        if (strip && symbol.binding == Binding::local && !referenced.contains(i)) continue;
        if (symbol.section >= 0) symbol.section = sections[static_cast<std::size_t>(symbol.section)];
        symbols[i] = result.symbols.size(); result.symbols.push_back(std::move(symbol));
    }
    for (auto r : object.relocations) if (sections[r.section] >= 0) {
        r.section = static_cast<std::size_t>(sections[r.section]); r.symbol = symbols[r.symbol]; result.relocations.push_back(r);
    }
    if (!verify_object(result, diagnostics)) return false;
    object = std::move(result); return true;
}
std::vector<std::uint64_t> interp_args(const std::string& text) {
    std::vector<std::uint64_t> args;
    if (!text.empty()) {
        std::istringstream list(text); std::string arg;
        while (std::getline(list, arg, ',')) {
            if (arg.empty()) throw Error(2, "interp --args requires comma-separated unsigned decimals");
            std::uint64_t value = 0;
            const auto result = std::from_chars(arg.data(), arg.data() + arg.size(), value);
            if (result.ec != std::errc{} || result.ptr != arg.data() + arg.size())
                throw Error(2, "interp --args requires comma-separated unsigned decimals: " + arg);
            args.push_back(value);
        }
    }
    return args;
}
// Shared dialect-backend emission for convertible modules. Modules using
// operations without an expression lowering (such as C memory or control
// operations) fail with an explicit diagnostic naming the operation.
int emit_shared_target(const Module& module, const std::string& triple, const std::string& args_text,
                       const std::string& output, const std::vector<std::string>& inputs, DiagnosticEngine& diagnostics) {
    if (triple == "llvmir-unknown-unknown") {
        auto emitted = emit_llvm_ir(module, diagnostics); if (!emitted) return 1;
        write_text(output, *emitted, inputs); return 0;
    }
    if (triple == "wasm32-unknown-unknown") {
        auto bytes = emit_wasm(module, diagnostics); if (!bytes) return 1;
        write(output, *bytes, inputs); return 0;
    }
    if (triple == "x86_64-unknown-linux-gnu") {
        auto object = emit_x86_object(module, diagnostics); if (!object) return 1;
        auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
        write(output, *bytes, inputs); return 0;
    }
    if (triple == "interp-unknown-unknown") {
        auto value = interpret(module, interp_args(args_text), diagnostics); if (!value) return 1;
        write_text(output, std::to_string(*value) + "\n", inputs); return 0;
    }
    throw Error(2, "unsupported emission target: " + triple);
}
int execute_tool(std::string_view name, const Options& opts, SourceManager& sources, DiagnosticEngine& diagnostics) {
    Context context; register_builtin_dialects(context, diagnostics);
    auto one = [&]() { if (opts.inputs.size() != 1) throw Error(2, "expected exactly one input file"); };
    auto protected_inputs = [&] {
        auto result = opts.inputs;
        result.insert(result.end(), opts.protected_inputs.begin(), opts.protected_inputs.end());
        for (Identifier id = 1; const auto* source = sources.file(id); ++id) result.push_back(source->name);
        return result;
    };
    if (name == "mlvm-sched" || (name == "mlvm" && (!opts.schedule.empty() ||
        (opts.inputs.size() == 1 && std::filesystem::path(opts.inputs[0]).extension() == ".sched")))) {
        if (!opts.convert_to.empty() || !opts.target.empty() || !opts.target_args.empty() || !opts.passes.empty() || !opts.mode.empty() || opts.dump)
            throw Error(2, "--convert-to/--target/--args/--passes/--verify/--print/--dump-passes belong to direct IR mode, not --schedule");
        std::string path = opts.schedule;
        if (path.empty()) { one(); path = opts.inputs[0]; }
        else if (!opts.inputs.empty()) throw Error(2, "--schedule takes its inputs from the orchestrator; do not pass positional IR files");
        auto registry = sched::Registry::builtins(context);
#ifdef MLVM_HAS_CEPHYR
        cephyr::register_scheduler_actions(registry, context);
#endif
        auto graph = sched::load(path, registry, diagnostics);
        if (!graph) return 1;
        if (opts.schedule_dot) { write_text(opts.output, graph->dot(diagnostics), {path}); return 0; }
        if (opts.schedule_check) return 0;
        auto order = graph->order(diagnostics);
        std::string selected = opts.result.empty() ? (order.empty() ? "" : order.back()) : opts.result;
        if (!selected.empty() && std::find(order.begin(), order.end(), selected) == order.end()) throw Error(2, "unknown result node: " + selected);
        sched::Result result;
        auto base = std::filesystem::absolute(path).parent_path();
        if (!sched::Scheduler{}.run(*graph, result, diagnostics, base)) return 1;
        if (opts.trace) for (const auto& event : result.events)
            std::cerr << event.node << ": " << event.action << (event.rewrites ? " [rewrite]" : "")
                      << (event.source.empty() ? "" : " " + event.source) << '\n';
        if (!selected.empty()) {
            const auto& artifact = result.artifacts.at(selected);
            auto inputs = protected_inputs(); inputs.push_back(path);
            for (const auto& file : result.read_paths) inputs.push_back(file.string());
            if (artifact.kind == "bytes") write(opts.output, artifact.get<std::vector<std::uint8_t>>(), inputs);
            else write_text(opts.output, sched::print_artifact(artifact), inputs);
        }
        return 0;
    }
    auto kernels = [&](Module& module, bool execute) {
        std::map<std::string, std::string> loaded;
        auto load = [&](const std::string& path, bool declaration = false) -> const std::string& {
            const auto key = std::filesystem::weakly_canonical(path).string();
            if (!loaded.contains(key)) {
                auto source = text(read(path));
                module.kernel_sources.push_back(source);
                loaded.emplace(key, std::move(source));
            } else if (declaration) module.kernel_sources.push_back(loaded.at(key));
            return loaded.at(key);
        };
        for (const auto& path : opts.kernel_files) load(path, true);
#ifdef MLVM_HAS_OPTILISP
        std::filesystem::path root = opts.blackstone;
        if (root.empty()) root = std::filesystem::is_directory(MLVM_BLACKSTONE_SOURCE_DIR)
            ? MLVM_BLACKSTONE_SOURCE_DIR : MLVM_BLACKSTONE_INSTALL_DIR;
        auto registry = root / "optilisp/stdlib";
        if (!std::filesystem::is_directory(registry)) registry = root / "stdlib";
        for (const auto& request : opts.kernel_applications) {
            if (!request.file) { module.kernel_applications.push_back(request.value); continue; }
            const auto definitions = optilisp::load_registry(registry);
            for (const auto& error : definitions.diagnostics) diagnostics.error(error.code, error.render());
            if (!definitions.ok()) return false;
            const optilisp::Loader loader(definitions.registry, {}, optilisp::Loader::Profile::blackstone);
            const auto selected = loader.load_source(load(request.value), request.value);
            for (const auto& error : selected.diagnostics) diagnostics.error(error.code, error.render());
            if (!selected.ok()) return false;
            if (selected.kernels.empty()) { diagnostics.error("optilisp.empty", "no kernels in " + request.value); return false; }
            for (const auto& kernel : selected.kernels) module.kernel_applications.push_back(kernel.id);
        }
        if (!execute || (module.kernel_sources.empty() && module.kernel_applications.empty())) return true;
        return optilisp::run_kernels(module, registry, root / "kernels", diagnostics);
#else
        for (const auto& request : opts.kernel_applications) module.kernel_applications.push_back(request.value);
        if (!execute || (module.kernel_sources.empty() && module.kernel_applications.empty())) return true;
        diagnostics.error("optilisp.unavailable", "kernel execution requires MLVM_ENABLE_OPTILISP=ON"); return false;
#endif
    };
    if (name == "mlvm" || name == "mlvm-as") {
        one(); const auto data = text(read(opts.inputs[0])); const auto file = sources.add(opts.inputs[0], data);
        if (name == "mlvm-as") {
            auto object = assemble(data, sources, file, diagnostics); if (!object) return 1;
            auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
            write(opts.output, *bytes, protected_inputs()); return 0;
        }
        if (name == "mlvm") {
            if (!opts.target_args.empty() && opts.target != "interp-unknown-unknown")
                throw Error(2, "--args/--arguments belong to the interp target");
            if (opts.mode == "--verify" && !opts.target.empty())
                throw Error(2, "--verify takes no --target; verify the IR instead");
            if (opts.mode == "--print" && !opts.target.empty())
                throw Error(2, "--print takes no --target; emit IR or drop --print");
            auto module = parse_ir(context, sources, file, diagnostics); if (!module) return 1;
            if (!kernels(*module, opts.mode != "--verify" && opts.mode != "--print")) return 1;
            PassManager passes; passes.dump_ir = opts.dump;
            if (opts.dump) passes.instrumentation = [](const PassEvent& event) { std::cerr << "// before " << event.name << '\n' << event.before << "// after " << event.name << '\n' << event.after; };
            if (!opts.passes.empty()) {
                std::istringstream input(opts.passes); std::string pass;
                while (std::getline(input, pass, ',')) {
                    if (pass == "constant-fold") passes.add(constant_fold_pass());
                    else if (pass == "canonicalize") passes.add(canonicalize_pass());
                    else if (pass == "dce") passes.add(dead_code_elimination_pass());
                    else throw Error(2, "unknown pass: " + pass);
                }
            }
            if (!passes.run(*module, diagnostics)) return 1;
            if (!opts.convert_to.empty() && !lower_dialect(*module, opts.convert_to, diagnostics)) return 1;
            if (opts.mode == "--verify") return 0;
            if (!opts.target.empty()) return emit_shared_target(*module, opts.target, opts.target_args, opts.output, protected_inputs(), diagnostics);
            write_text(opts.output, print_ir(*module), protected_inputs());
            return 0;
        }
    }
    if (name == "cephyr") {
#ifdef MLVM_HAS_CEPHYR
        if (!opts.target_args.empty() && opts.emit_target != "interp-unknown-unknown")
            throw Error(2, "--args/--arguments belong to the interp target");
        const bool linking = opts.mode.empty() && opts.emit_target.empty();
        if (!linking) one();
        std::vector<Object> objects;
        std::vector<std::string> link_files;
        driver_detail::ToolTemporary temporary;
        auto external = [&](const std::string& command, const std::vector<std::string>& arguments) {
            const auto status = driver_detail::run_process(command, arguments);
            if (status) throw Error(status, "tool '" + command + "' failed with status " + std::to_string(status));
        };
        auto add_link_file = [&](const std::string& path) {
            if (opts.linker == "mlvm") return link_input(path, objects, diagnostics);
            link_files.push_back(std::filesystem::absolute(path).string()); return true;
        };
        auto runtime_file = [&](const char* filename) {
            if (opts.preprocessor.sysroot.empty()) throw Error(2, "no runtime sysroot configured; use --sysroot or -nostdlib");
            return (opts.preprocessor.sysroot / "lib" / filename).string();
        };
        auto protected_files = protected_inputs();
        if (linking && !opts.no_startfiles) {
            auto start = runtime_file("crt0.o");
            protected_files.push_back(start);
            if (!add_link_file(start)) return 1;
        }
        for (const auto& input : opts.inputs) {
        const auto extension = std::filesystem::path(input).extension();
        if (linking && (extension == ".o" || extension == ".a")) {
            if (!add_link_file(input)) return 1;
            continue;
        }
        const auto file = sources.add(input, text(read(input)));
        auto preprocessed = cephyr::preprocess(sources, file, opts.preprocessor, diagnostics); if (!preprocessed) return 1;
        if (opts.mode == "-E") { write_text(opts.output, *preprocessed, protected_inputs()); return 0; }
        const auto expanded = sources.add(input + "<preprocessed>", *preprocessed);
        if (opts.mode == "--parse-only") return cephyr::parse(sources, expanded, diagnostics) ? 0 : 1;
        auto module = cephyr::lower(context, sources, expanded, diagnostics); if (!module) return 1;
        if (opts.mode == "-fsyntax-only") return 0;
        if (!kernels(*module, true)) return 1;
        // Dialect-based backends for convertible modules. Modules using C
        // memory, address, call or control operations fail here with an
        // explicit diagnostic naming the blocking operation.
        if (!opts.emit_target.empty())
            return emit_shared_target(*module, opts.emit_target, opts.target_args, opts.output, protected_inputs(), diagnostics);
        if (opts.mode == "--emit-ir") { write_text(opts.output, print_ir(*module), protected_inputs()); return 0; }
        auto assembly = cephyr::emit_assembly(*module, diagnostics); if (!assembly) return 1;
        if (opts.mode == "-S") { write_text(opts.output, *assembly, protected_inputs()); return 0; }
        std::optional<std::vector<std::uint8_t>> bytes;
        if (opts.assembler == "mlvm") {
            const auto asm_file = sources.add(input + "<assembly>", *assembly);
            auto object = assemble(*assembly, sources, asm_file, diagnostics); if (!object) return 1;
            if (linking && opts.linker == "mlvm") { objects.push_back(std::move(*object)); continue; }
            bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
        } else {
            const auto asm_path = temporary.file("input.s"), obj_path = temporary.file("assembled.o");
            write_text(asm_path, *assembly, {});
            std::filesystem::remove(obj_path);
            external(opts.assembler, {"-o", obj_path, asm_path});
            bytes = read(obj_path);
            if (!read_elf(*bytes, diagnostics)) return 1;
        }
        if (linking) {
            const auto path = temporary.file("unit-" + std::to_string(link_files.size()) + ".o");
            write(path, *bytes, {});
            if (!add_link_file(path)) return 1;
            continue;
        }
        const auto output = opts.output.empty() ? std::filesystem::path(input).filename().replace_extension(".o").string() : opts.output;
        write(output, *bytes, protected_inputs()); return 0;
        }
        if (!opts.no_defaultlibs) {
            auto library = runtime_file("libc.a");
            protected_files.push_back(library);
            if (!add_link_file(library)) return 1;
        }
        std::optional<std::vector<std::uint8_t>> executable;
        if (opts.linker == "mlvm") executable = link_executable(objects, opts.entry, diagnostics);
        else {
            const auto output = temporary.file("executable");
            std::vector<std::string> arguments{"-o", output, "-e", opts.entry};
            arguments.insert(arguments.end(), link_files.begin(), link_files.end());
            external(opts.linker, arguments);
            executable = read(output);
        }
        if (!executable) return 1;
        const auto sources_to_protect = protected_inputs();
        protected_files.insert(protected_files.end(), sources_to_protect.begin(), sources_to_protect.end());
        write(opts.output.empty() ? "a.out" : opts.output, *executable, protected_files, true);
        return 0;
#else
        throw Error(2, "Cephyr was disabled at build time");
#endif
    }
    if (name == "mlvm-strings") {
        for (const auto& input : opts.inputs) {
            auto bytes = read(input); std::string current;
            auto flush = [&] { if (current.size() >= opts.minimum) std::cout << current << '\n'; current.clear(); };
            for (auto byte : bytes) { if (byte >= 32 && byte <= 126) current += static_cast<char>(byte); else flush(); } flush();
        }
        return 0;
    }
    if (name == "mlvm-ar") {
        if (opts.archive_command == "t") {
            one(); auto members = read_archive(read(opts.inputs[0]), diagnostics); if (!members) return 3;
            for (const auto& member : *members) std::cout << member.name << '\n';
            return 0;
        }
        if (opts.archive_command != "q") throw Error(2, "expected archive command t or q");
        std::vector<ArchiveMember> members;
        for (const auto& input : opts.inputs) members.push_back({std::filesystem::path(input).filename().string(), read(input)});
        auto bytes = write_archive(members, diagnostics); if (!bytes) return 1;
        write(opts.output, *bytes, protected_inputs()); return 0;
    }
    if (name == "mlvm-ld") {
        std::vector<Object> objects;
        for (const auto& input : opts.inputs) {
            if (!link_input(input, objects, diagnostics)) return 3;
        }
        std::optional<std::vector<std::uint8_t>> output;
        if (opts.relocatable) { auto object = link_relocatable(objects, diagnostics); if (object) output = write_elf(*object, diagnostics); }
        else output = link_executable(objects, opts.entry, diagnostics);
        if (!output) return 1;
        write(opts.output, *output, protected_inputs(), !opts.relocatable); return 0;
    }
    if (name == "mlvm-objcopy" || name == "mlvm-strip") {
        one(); auto object = read_elf(read(opts.inputs[0]), diagnostics); if (!object) return 3;
        if (!transform(*object, opts, name == "mlvm-strip", diagnostics)) return 1;
        if (opts.raw) {
            if (object->sections.size() != 1 || !object->relocations.empty()) throw Error(1, "raw extraction requires one selected section without relocations");
            write(opts.output, object->sections[0].data, protected_inputs()); return 0;
        }
        auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
        write(opts.output, *bytes, protected_inputs()); return 0;
    }
    for (const auto& input : opts.inputs) {
        auto object = read_elf(read(input), diagnostics); if (!object) return 3;
        if (opts.inputs.size() > 1) std::cout << input << ":\n";
        inspect(*object, name, opts.disassemble);
    }
    return 0;
}
}
int run_tool(std::string_view name, int argc, char** argv) {
    SourceManager sources; DiagnosticEngine diagnostics;
    int status = 0;
    try {
        std::vector<std::string> args, response_inputs; std::set<std::string> active;
        for (int i = 1; i < argc; ++i) expand_response(argv[i], args, active, response_inputs);
        if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) { help(name); return 0; }
        if (args.size() == 1 && (args[0] == "--version" || args[0] == "-V")) { std::cout << name << " (MLVM) " << version << '\n'; return 0; }
        auto opts = options(name, args);
        opts.protected_inputs.insert(opts.protected_inputs.end(), response_inputs.begin(), response_inputs.end());
        status = execute_tool(name, opts, sources, diagnostics);
    } catch (const Error& error) { std::cerr << name << ": " << error.what() << '\n'; status = error.status; }
    catch (const std::filesystem::filesystem_error& error) { std::cerr << name << ": " << error.what() << '\n'; status = 3; }
    catch (const std::exception& error) { std::cerr << name << ": " << error.what() << '\n'; status = 1; }
    for (const auto& diagnostic : diagnostics.diagnostics()) std::cerr << sources.format(diagnostic) << '\n';
    if (!std::cout) return 3;
    return status;
}
} // namespace mlvm
