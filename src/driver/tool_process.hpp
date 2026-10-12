#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace mlvm::driver_detail {
class ToolTemporary {
    std::filesystem::path directory;
public:
    ToolTemporary() = default;
    ToolTemporary(const ToolTemporary&) = delete;
    ToolTemporary& operator=(const ToolTemporary&) = delete;
    ~ToolTemporary() { if (!directory.empty()) { std::error_code ec; std::filesystem::remove_all(directory, ec); } }
    std::string file(const std::string& name) {
        if (directory.empty()) {
#ifdef _WIN32
            throw std::runtime_error("external tool selection requires a POSIX platform");
#else
            auto pattern = (std::filesystem::temp_directory_path() / "cephyr-tools-XXXXXX").string();
            if (!::mkdtemp(pattern.data())) throw std::runtime_error("cannot create tool temporary directory: " + std::string(std::strerror(errno)));
            directory = pattern;
#endif
        }
        return (directory / name).string();
    }
};
inline int run_process(const std::string& command, const std::vector<std::string>& arguments) {
#ifdef _WIN32
    (void)command; (void)arguments;
    throw std::runtime_error("external tool selection requires a POSIX platform");
#else
    std::vector<char*> argv{const_cast<char*>(command.c_str())};
    for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    const auto child = ::fork();
    if (child < 0) throw std::runtime_error("cannot start '" + command + "': " + std::strerror(errno));
    if (!child) {
        ::execvp(command.c_str(), argv.data());
        ::_exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("cannot wait for '" + command + "'");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}
} // namespace mlvm::driver_detail
