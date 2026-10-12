// Optlilisp.hpp -- C++ RAII wrapper over the Optlilisp C loader.
//
// Mirrors firestone header conventions (#pragma once, header-only where
// practical). Depends only on Optlilisp.h and the C++ standard library.

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Optlilisp.h"

namespace optlilisp {

struct Location {
    std::string file;
    int line = 0;
    int column = 0;
};

struct Diagnostic {
    std::string severity;
    std::string code;
    std::string message;
    Location location;
    bool has_location = false;
};

class Error : public std::runtime_error {
public:
    explicit Error(const std::string &message) : std::runtime_error(message) {}
};

struct ProgramDeleter {
    void operator()(OptlilispProgram *p) const noexcept
    {
        if (p) {
            optlilisp_program_fini(p);
            delete p;
        }
    }
};

using ProgramPtr = std::unique_ptr<OptlilispProgram, ProgramDeleter>;

inline std::vector<Diagnostic> collect_diagnostics(const OptlilispSink &sink)
{
    std::vector<Diagnostic> out;
    for (size_t i = 0; i < sink.count; i++) {
        const OptlilispDiag &d = sink.items[i];
        Diagnostic diag;
        diag.severity = d.severity;
        diag.code = d.code;
        diag.message = d.message;
        diag.has_location = d.has_loc != 0;
        if (d.has_loc) {
            diag.location.file = d.loc.file ? d.loc.file : "";
            diag.location.line = d.loc.line;
            diag.location.column = d.loc.column;
        }
        out.push_back(std::move(diag));
    }
    return out;
}

inline std::string render_diagnostic(const Diagnostic &d)
{
    std::string out;
    if (d.has_location)
        out += d.location.file + ":" + std::to_string(d.location.line) + ":" +
               std::to_string(d.location.column) + ": ";
    out += d.severity + ": [" + d.code + "] " + d.message;
    return out;
}

class Program {
public:
    Program() : prog_(new OptlilispProgram)
    {
        optlilisp_program_init(prog_.get());
    }

    Program(const Program &) = delete;
    Program &operator=(const Program &) = delete;
    Program(Program &&) = default;
    Program &operator=(Program &&) = default;

    static Program parse(const std::string &source, const std::string &filename,
                         std::vector<Diagnostic> *diagnostics = nullptr)
    {
        Program prog;
        OptlilispSink sink;
        optlilisp_sink_init(&sink);
        optlilisp_parse(source.c_str(), filename.c_str(), prog.prog_.get(), &sink);
        if (diagnostics)
            *diagnostics = collect_diagnostics(sink);
        bool ok = !optlilisp_sink_has_errors(&sink);
        optlilisp_sink_fini(&sink);
        if (!ok)
            throw Error("optlilisp: parse failed for '" + filename + "'");
        return prog;
    }

    static Program parse_file(const std::string &path,
                              std::vector<Diagnostic> *diagnostics = nullptr)
    {
        Program prog;
        OptlilispSink sink;
        optlilisp_sink_init(&sink);
        optlilisp_parse_file(path.c_str(), prog.prog_.get(), &sink);
        if (diagnostics)
            *diagnostics = collect_diagnostics(sink);
        bool ok = !optlilisp_sink_has_errors(&sink);
        optlilisp_sink_fini(&sink);
        if (!ok)
            throw Error("optlilisp: failed to load '" + path + "'");
        return prog;
    }

    size_t size() const { return prog_->count; }
    OptlilispNode *form(size_t i) const
    {
        return i < prog_->count ? prog_->forms[i] : nullptr;
    }

    OptlilispProgram *get() const { return prog_.get(); }

private:
    ProgramPtr prog_;
};

struct KernelInfo {
    std::string id;
    std::string name;
    std::string stage;
    std::string verb;
    bool has_id = false;
    bool has_name = false;
    bool has_stage = false;
    bool has_transform = false;
};

inline bool is_define_kernel(const OptlilispNode *node)
{
    return optlilisp_is_define_kernel(node) != 0;
}

inline KernelInfo kernel_info(const OptlilispNode *define_kernel)
{
    OptlilispKernelInfo raw;
    KernelInfo out;
    if (optlilisp_kernel_info(define_kernel, &raw) != 0)
        throw Error("optlilisp: node is not a (define-kernel ...) form");
    out.id = raw.id;
    out.name = raw.name;
    out.stage = raw.stage;
    out.verb = raw.verb;
    out.has_id = raw.has_id != 0;
    out.has_name = raw.has_name != 0;
    out.has_stage = raw.has_stage != 0;
    out.has_transform = raw.has_transform != 0;
    return out;
}

inline bool is_stage(const std::string &stage)
{
    return optlilisp_is_stage(stage.c_str()) != 0;
}

inline bool is_verb(const std::string &verb)
{
    return optlilisp_is_verb(verb.c_str()) != 0;
}

inline bool verb_ok_at_stage(const std::string &verb, const std::string &stage)
{
    return optlilisp_verb_ok_at_stage(verb.c_str(), stage.c_str()) != 0;
}

} // namespace optlilisp
