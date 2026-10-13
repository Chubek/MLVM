#pragma once
#include <mlvm/ir.hpp>
#include <mlvm/blackstone.hpp>
#include <libir/RawDAG.hpp>
#include <libir/DataflowGraph.hpp>
#include <libir/Typed3AC.hpp>
#include <libir/SimpleSSAGraph.hpp>
#include <any>
#include <filesystem>
#include <memory>

namespace mlvm::sched {
// Value-owned artifacts: each stage receives immutable predecessors and returns a new value.
// Custom artifacts must obey the same copy/ownership contract.
struct Artifact {
    std::string kind;
    std::any value;
    template<class T> const T& get() const { return std::any_cast<const T&>(value); }
};
struct Dataflow {
    firestone::ir::DataflowGraph graph;
    std::vector<firestone::ir::NodeId> roots;
    bool is_signed = true;
};
struct Event { std::string node, action; std::size_t rewrites = 0; std::string source; };
struct RunContext {
    DiagnosticEngine& diagnostics;
    std::vector<Event>& events;
    std::filesystem::path base_directory;
    std::set<std::filesystem::path>& read_paths;
};
using Action = std::function<std::optional<Artifact>(std::span<const Artifact* const>, RunContext&)>;
struct Node { std::string name, action; std::vector<std::string> inputs, after; Action run; };

class Graph {
public:
    bool add(Node, DiagnosticEngine&);
    // Forward references are allowed until validation. Input order is argument order.
    bool validate(DiagnosticEngine&) const;
    std::vector<std::string> order(DiagnosticEngine&) const;
    std::string dot(DiagnosticEngine&) const;
    const std::vector<Node>& nodes() const { return nodes_; }
private:
    std::optional<firestone::ir::RawDAG<std::size_t>> build(DiagnosticEngine&) const;
    std::vector<Node> nodes_;
};
struct Result {
    std::map<std::string, Artifact, std::less<>> artifacts;
    std::vector<Event> events;
    std::set<std::filesystem::path> read_paths;
};
class Scheduler {
public:
    // Whole-run publication: out is untouched if any node fails.
    bool run(const Graph&, Result& out, DiagnosticEngine&,
             const std::filesystem::path& base_directory = ".") const;
};
using Options = std::map<std::string, std::string, std::less<>>;
using ActionFactory = std::function<Action(const Options&)>;
class Registry {
public:
    bool add(std::string name, ActionFactory);
    Action make(std::string_view name, const Options&) const;
    static Registry builtins(Context&);
private:
    std::map<std::string, ActionFactory, std::less<>> factories_;
};
// Lua lives only during loading. The resulting graph owns all action parameters.
std::optional<Graph> load(const std::filesystem::path&, const Registry&, DiagnosticEngine&);
std::optional<Graph> parse(std::string_view source, std::string_view name,
                           const Registry&, DiagnosticEngine&);

class Kernel {
public:
    static std::shared_ptr<Kernel> load(const std::filesystem::path&,
        std::span<const std::filesystem::path> search_directories, DiagnosticEngine&);
    static std::shared_ptr<Kernel> read_profile(const std::filesystem::path&, DiagnosticEngine&);
    ~Kernel();
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;
    const BSProfile* profile() const { return profile_; }
    std::vector<std::string> rules() const;
    bool write_profile(const std::filesystem::path&, DiagnosticEngine&) const;
private:
    Kernel() = default;
    BSRegistry* registry_ = nullptr;
    BSProfile* profile_ = nullptr;
};
// Host proofs for already-produced SSA values. Unknown is the conservative default.
using Facts = std::function<BSFact(ValueId, std::string_view)>;
bool apply(const Kernel&, Module&, std::size_t budget, std::vector<Event>&, DiagnosticEngine&, const Facts& = {});
bool apply(const Kernel&, Dataflow&, std::size_t budget, std::vector<Event>&, DiagnosticEngine&);
bool verify_artifact(const Artifact&, DiagnosticEngine&);
std::string print_artifact(const Artifact&);
std::optional<Dataflow> to_dataflow(const Module&, DiagnosticEngine&);
std::optional<Module> from_dataflow(const Dataflow&, Context&, DiagnosticEngine&);
firestone::ir::Typed3AC to_three_address(const Dataflow&);
firestone::ir::SimpleSSAGraph to_ssa(const Dataflow&);
} // namespace mlvm::sched
