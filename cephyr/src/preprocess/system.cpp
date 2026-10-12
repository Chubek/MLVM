#include "engines.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cephyr::preprocess_detail {
namespace {
constexpr std::size_t kOutputLimit = 64 * 1024 * 1024;
constexpr std::size_t kLogLimit = 1024 * 1024;

#ifdef _WIN32
std::optional<std::string> unsupported(mlvm::DiagnosticEngine& diagnostics) {
    diagnostics.error("cephyr.preprocess", "the system preprocessor engine is only available on POSIX platforms; use ucpp");
    return std::nullopt;
}
#else

std::string find_on_path(const std::string& command) {
    if (command.find('/') != std::string::npos)
        return ::access(command.c_str(), X_OK) == 0 ? command : "";
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::string_view search(path);
    while (true) {
        const auto end = search.find(':');
        const std::string directory(search.substr(0, end));
        const std::string candidate = (directory.empty() ? "." : directory) + "/" + command;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        if (end == std::string_view::npos) return "";
        search.remove_prefix(end + 1);
    }
}

// Reads a stream fully up to limit+1 bytes to detect overflow.
bool read_limited(std::FILE* stream, std::string& text, std::size_t limit) {
    char buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        if (text.size() + got > limit + 1) return false;
        text.append(buffer, got);
    }
    return true;
}

struct TempInput {
    std::string directory;
    std::string path;
    bool live = false;
    ~TempInput() {
        if (!live) return;
        ::unlink(path.c_str());
        ::rmdir(directory.c_str());
    }
};
#endif
} // namespace

std::optional<std::string> preprocess_system(mlvm::SourceManager& sources, mlvm::Identifier file,
                                             const PreprocessOptions& options,
                                             mlvm::DiagnosticEngine& diagnostics) {
#ifdef _WIN32
    (void)sources; (void)file; (void)options;
    return unsupported(diagnostics);
#else
    const auto* source = sources.file(file);
    if (!source || source->text.find('\0') != std::string::npos) {
        diagnostics.error("cephyr.preprocess", "invalid source");
        return std::nullopt;
    }

    std::string command;
    if (!options.system_command.empty()) {
        command = find_on_path(options.system_command);
        if (command.empty()) {
            diagnostics.error("cephyr.preprocess", "system preprocessor not found: " + options.system_command);
            return std::nullopt;
        }
    } else {
        for (const char* candidate : {"cc", "gcc", "clang", "cpp"}) {
            command = find_on_path(candidate);
            if (!command.empty()) break;
        }
        if (command.empty()) {
            diagnostics.error("cephyr.preprocess", "no system preprocessor found (tried cc, gcc, clang, cpp); use ucpp");
            return std::nullopt;
        }
    }

    // Stage the input beside the original source when possible so quoted
    // includes resolve as they would for the real file.
    std::string base = std::filesystem::path(source->name).parent_path().string();
    if (base.empty() || !std::filesystem::is_directory(base)) {
        std::error_code ec;
        base = std::filesystem::temp_directory_path(ec).string();
        if (ec || base.empty()) base = "/tmp";
    }
    TempInput staged;
    staged.directory = base + "/cephyr-pp-XXXXXX";
    std::vector<char> template_buffer(staged.directory.begin(), staged.directory.end());
    template_buffer.push_back('\0');
    if (!::mkdtemp(template_buffer.data())) {
        diagnostics.error("cephyr.preprocess", "cannot create preprocessor staging directory");
        return std::nullopt;
    }
    staged.directory = template_buffer.data();
    staged.path = staged.directory + "/input.c";
    staged.live = true;
    const int fd = ::open(staged.path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) {
        diagnostics.error("cephyr.preprocess", "cannot stage preprocessor input");
        return std::nullopt;
    }
    std::size_t written = 0;
    while (written < source->text.size()) {
        const auto chunk = ::write(fd, source->text.data() + written, source->text.size() - written);
        if (chunk < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            diagnostics.error("cephyr.preprocess", "cannot stage preprocessor input");
            return std::nullopt;
        }
        written += static_cast<std::size_t>(chunk);
    }
    ::close(fd);

    // GCC-compatible freestanding invocation. -P suppresses linemarkers so the
    // output stays parseable; -undef/-nostdinc keep the translation unit
    // freestanding. User -I/-D/-U/-include follow and may override.
    std::vector<std::string> storage{command, "-E", "-P", "-undef", "-std=c11", "-ffreestanding", "-nostdinc"};
    for (const auto& directory : options.include_paths) {
        storage.emplace_back("-I");
        storage.push_back(directory.string());
    }
    for (const auto& [name, value] : options.defines) {
        storage.emplace_back("-D");
        storage.push_back(name + "=" + value);
    }
    for (const auto& name : options.undefines) {
        storage.emplace_back("-U");
        storage.push_back(name);
    }
    for (const auto& include : options.forced_includes) {
        storage.emplace_back("-include");
        storage.push_back(include.string());
    }
    storage.push_back(staged.path);
    std::vector<char*> argv;
    for (auto& arg : storage) argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::FILE* captured_out = std::tmpfile();
    std::FILE* captured_err = std::tmpfile();
    if (!captured_out || !captured_err) {
        if (captured_out) std::fclose(captured_out);
        if (captured_err) std::fclose(captured_err);
        diagnostics.error("cephyr.preprocess", "cannot capture preprocessor output");
        return std::nullopt;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        std::fclose(captured_out);
        std::fclose(captured_err);
        diagnostics.error("cephyr.preprocess", "cannot spawn system preprocessor");
        return std::nullopt;
    }
    if (child == 0) {
        ::dup2(::fileno(captured_out), STDOUT_FILENO);
        ::dup2(::fileno(captured_err), STDERR_FILENO);
        ::execvp(command.c_str(), argv.data());
        ::_exit(127); // Only reached when execvp fails.
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            std::fclose(captured_out);
            std::fclose(captured_err);
            diagnostics.error("cephyr.preprocess", "cannot wait for system preprocessor");
            return std::nullopt;
        }
    }
    std::string output, log;
    std::rewind(captured_out);
    std::rewind(captured_err);
    const bool output_ok = read_limited(captured_out, output, kOutputLimit);
    const bool log_ok = read_limited(captured_err, log, kLogLimit);
    std::fclose(captured_out);
    std::fclose(captured_err);
    while (!log.empty() && (log.back() == '\n' || log.back() == '\r')) log.pop_back();
    if (!log_ok) log += "\n[truncated]";

    const bool exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (WIFSIGNALED(status)) {
        diagnostics.error("cephyr.preprocess",
                          "system preprocessor killed by signal " + std::to_string(WTERMSIG(status)) +
                              (log.empty() ? "" : ": " + log));
        return std::nullopt;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        diagnostics.error("cephyr.preprocess", "cannot execute system preprocessor: " + command);
        return std::nullopt;
    }
    if (!exited) {
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        diagnostics.error("cephyr.preprocess",
                          "system preprocessor failed (exit " + std::to_string(code) + ")" +
                              (log.empty() ? "" : ": " + log));
        return std::nullopt;
    }
    if (!output_ok) {
        diagnostics.error("cephyr.preprocess", "preprocessed output exceeds 64 MiB");
        return std::nullopt;
    }
    if (!log.empty())
        diagnostics.report({mlvm::Severity::warning, "cephyr.preprocess", "system preprocessor: " + log, {}, {}, {}});
    return output;
#endif
}
} // namespace cephyr::preprocess_detail
