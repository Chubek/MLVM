#include "engines.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" {
#include "cpp.h"
}

namespace cephyr::preprocess_detail {
namespace {
constexpr std::size_t kOutputLimit = 64 * 1024 * 1024;

std::mutex ucpp_mutex;
struct RunState {
    mlvm::DiagnosticEngine* diagnostics = nullptr;
    mlvm::Identifier file = 0;
    bool failed = false;
};
RunState* current_run = nullptr; // Guarded by ucpp_mutex.
std::once_flag ucpp_init;
} // namespace

void report(long line, bool is_error, char* format, std::va_list args) {
    if (!current_run || !current_run->diagnostics) return;
    char message[4096];
    std::vsnprintf(message, sizeof(message), format, args);
    std::string text = message;
    if (current_filename && current_filename[0] != '\0')
        text = std::string(current_filename) + ": " + text;
    mlvm::SourceLocation location{};
    if (line > 0) location = {current_run->file, static_cast<std::uint32_t>(line), 1};
    if (is_error) {
        current_run->diagnostics->error("cephyr.preprocess", text, location);
        current_run->failed = true;
    } else {
        current_run->diagnostics->report({mlvm::Severity::warning, "cephyr.preprocess", text, location, {}, {}});
    }
}

// Replaces ucpp's stderr writers (NO_UCPP_ERROR_FUNCTIONS). Recording keeps
// library diagnostics structured; the run fails at the end if any error was
// recorded. Warnings never fail the run.
struct UcppSession {
    RunState state;
    struct lexer_state lexer{};
    bool lexer_live = false;
    std::FILE* input = nullptr;
    std::FILE* output = nullptr;
    UcppSession(mlvm::DiagnosticEngine& diagnostics, mlvm::Identifier file) { state = {&diagnostics, file, false}; }
    ~UcppSession() {
        if (lexer_live) {
            // free_lexer_state closes the input stream via close_input().
            free_lexer_state(&lexer);
            input = nullptr;
        }
        wipeout();
        if (input) std::fclose(input);
        if (output) std::fclose(output);
        current_run = nullptr;
    }
};

// Escapes a forced-include path for a quoted #include directive.
bool escape_include(const std::filesystem::path& path, std::string& directive, mlvm::DiagnosticEngine& diagnostics) {
    const std::string name = path.string();
    if (name.find('\0') != std::string::npos || name.find('\n') != std::string::npos) {
        diagnostics.error("cephyr.preprocess", "forced include path is not representable: " + name);
        return false;
    }
    directive = "#include \"";
    for (char c : name) {
        if (c == '\\' || c == '"') directive += '\\';
        directive += c;
    }
    directive += "\"\n";
    return true;
}

std::optional<std::string> preprocess_ucpp(mlvm::SourceManager& sources, mlvm::Identifier file,
                                           const PreprocessOptions& options,
                                           mlvm::DiagnosticEngine& diagnostics) {
    const auto* source = sources.file(file);
    if (!source || source->text.find('\0') != std::string::npos) {
        diagnostics.error("cephyr.preprocess", "invalid source");
        return std::nullopt;
    }
    std::string staged;
    for (const auto& include : options.forced_includes) {
        std::string directive;
        if (!escape_include(include, directive, diagnostics)) return std::nullopt;
        staged += directive;
    }
    staged += source->text;
    if (!staged.empty() && staged.back() != '\n') staged += '\n';

    std::lock_guard<std::mutex> lock(ucpp_mutex);
    std::call_once(ucpp_init, [] { init_cpp(); });
    UcppSession session(diagnostics, file);
    current_run = &session.state;

    session.input = std::tmpfile();
    session.output = std::tmpfile();
    if (!session.input || !session.output) {
        diagnostics.error("cephyr.preprocess", "cannot create preprocessor staging stream");
        return std::nullopt;
    }
    if (std::fwrite(staged.data(), 1, staged.size(), session.input) != staged.size()) {
        diagnostics.error("cephyr.preprocess", "cannot stage preprocessor input");
        return std::nullopt;
    }
    std::rewind(session.input);

    no_special_macros = 0;
    c99_compliant = 0; // __STDC_VERSION__ is defined below as 201112L, not 199901L.
    c99_hosted = 0;    // __STDC_HOSTED__ is defined below as 0.
    emit_defines = emit_assertions = emit_dependencies = 0;
    init_tables(0);
    init_include_path(nullptr);
    emit_output = session.output;

    init_lexer_state(&session.lexer);
    session.lexer_live = true;
    session.lexer.flags = DISCARD_COMMENTS | CPLUSPLUS_COMMENTS | TEXT_OUTPUT | KEEP_OUTPUT |
                          HANDLE_TRIGRAPHS | MACRO_VAARG | WARN_STANDARD | FAIL_SHARP;
    session.lexer.input = session.input;
    session.lexer.output = session.output;
    // Quoted includes resolve relative to the original source location, not
    // the staging stream.
    std::string filename = source->name;
    if (filename.empty()) filename = "<input>";
    set_init_filename(filename.data(), 0);
    for (const auto& directory : options.include_paths) {
        std::string path = directory.string();
        if (path.empty()) path = ".";
        add_incpath(path.data());
    }
    auto define = [&](std::string spelling) {
        // Command-line definitions override the freestanding defaults below.
        undef_macro(&session.lexer, spelling.data());
        if (define_macro(&session.lexer, spelling.data())) session.state.failed = true;
    };
    define("__STDC_VERSION__=201112L");
    define("__STDC_HOSTED__=0");
    for (const auto& name : options.undefines) {
        std::string spelling = name;
        if (undef_macro(&session.lexer, spelling.data())) session.state.failed = true;
    }
    for (const auto& [name, value] : options.defines) define(name + "=" + value);

    enter_file(&session.lexer, session.lexer.flags);
    int status = 0;
    bool chunk_failed = false;
    while ((status = cpp(&session.lexer)) < CPPERR_EOF) {
        if (status > 0) chunk_failed = true;
    }
    check_cpp_errors(&session.lexer);

    std::string output;
    std::rewind(session.output);
    char buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), session.output)) > 0) {
        if (output.size() + got > kOutputLimit) {
            diagnostics.error("cephyr.preprocess", "preprocessed output exceeds 64 MiB");
            return std::nullopt;
        }
        output.append(buffer, got);
    }
    if (session.state.failed || chunk_failed) return std::nullopt;
    return output;
}
} // namespace cephyr::preprocess_detail

// ucpp diagnostic entry points. Declared in cpp.h; defined here so failures
// are recorded instead of printed.
extern "C" void ucpp_error(long line, char* format, ...) {
    std::va_list args;
    va_start(args, format);
    cephyr::preprocess_detail::report(line, true, format, args);
    va_end(args);
}
extern "C" void ucpp_warning(long line, char* format, ...) {
    std::va_list args;
    va_start(args, format);
    cephyr::preprocess_detail::report(line, false, format, args);
    va_end(args);
}
extern "C" void ucpp_ouch(char* format, ...) {
    char message[4096];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    // Unreachable without AUDIT/MEM_DEBUG: ucpp never calls ouch() in this
    // configuration. Abort rather than continue with corrupt state.
    std::fprintf(stderr, "cephyr: ucpp internal error: %s\n", message);
    std::abort();
}
