#include <mlvm/sched.hpp>
#include <set>

namespace mlvm::sched {
bool Graph::add(Node node, DiagnosticEngine& d) {
    if (node.name.empty() || node.name.find('\0') != std::string::npos || node.action.empty() || !node.run) {
        d.error("sched.node", "a node requires a nonempty name, action and implementation"); return false;
    }
    for (const auto& old : nodes_) if (old.name == node.name) {
        d.error("sched.duplicate", "duplicate node '" + node.name + "'"); return false;
    }
    nodes_.push_back(std::move(node)); return true;
}
std::optional<firestone::ir::RawDAG<std::size_t>> Graph::build(DiagnosticEngine& d) const {
    firestone::ir::RawDAG<std::size_t> dag;
    std::map<std::string, std::size_t, std::less<>> ids;
    for (std::size_t i = 0; i < nodes_.size(); ++i) ids.emplace(nodes_[i].name, dag.add_node(i));
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        std::set<std::string> dependencies(nodes_[i].inputs.begin(), nodes_[i].inputs.end());
        dependencies.insert(nodes_[i].after.begin(), nodes_[i].after.end());
        for (const auto& name : dependencies) {
            auto found = ids.find(name);
            if (found == ids.end()) { d.error("sched.dependency", "node '" + nodes_[i].name + "' references unknown node '" + name + "'"); return {}; }
            try { dag.add_edge(found->second, i); }
            catch (const std::invalid_argument&) {
                d.error("sched.cycle", "dependency '" + name + "' -> '" + nodes_[i].name + "' introduces a cycle"); return {};
            }
        }
    }
    return dag;
}
bool Graph::validate(DiagnosticEngine& d) const { return build(d).has_value(); }
std::vector<std::string> Graph::order(DiagnosticEngine& d) const {
    std::vector<std::string> result;
    if (auto dag = build(d)) for (auto id : dag->topological_order()) result.push_back(nodes_[id].name);
    return result;
}
std::string Graph::dot(DiagnosticEngine& d) const {
    auto dag = build(d);
    return dag ? dag->to_dot([&](std::size_t id) { return nodes_[id].name + "\n" + nodes_[id].action; }) : "";
}
bool Scheduler::run(const Graph& graph, Result& out, DiagnosticEngine& d, const std::filesystem::path& base) const {
    if (!graph.validate(d)) return false;
    Result candidate;
    RunContext context{d, candidate.events, base, candidate.read_paths};
    for (const auto& name : graph.order(d)) {
        const auto& nodes = graph.nodes();
        const auto& node = *std::find_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.name == name; });
        std::vector<const Artifact*> inputs;
        for (const auto& input : node.inputs) inputs.push_back(&candidate.artifacts.at(input));
        const auto before = d.diagnostics().size();
        try {
            auto artifact = node.run(inputs, context);
            bool errors = false;
            for (std::size_t i = before; i < d.diagnostics().size(); ++i)
                errors |= d.diagnostics()[i].severity == Severity::error || d.diagnostics()[i].severity == Severity::fatal;
            if (!artifact || errors || !verify_artifact(*artifact, d)) {
                d.error("sched.failed", "node '" + name + "' (" + node.action + ") failed"); return false;
            }
            candidate.artifacts.emplace(name, std::move(*artifact));
            candidate.events.push_back({name, node.action, 0, {}});
        } catch (const std::exception& e) {
            d.error("sched.failed", "node '" + name + "' (" + node.action + "): " + e.what()); return false;
        }
    }
    out = std::move(candidate); return true;
}
bool Registry::add(std::string name, ActionFactory factory) {
    return !name.empty() && factory && factories_.emplace(std::move(name), std::move(factory)).second;
}
Action Registry::make(std::string_view name, const Options& options) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) throw std::invalid_argument("unknown scheduler action '" + std::string(name) + "'");
    return it->second(options);
}
} // namespace mlvm::sched
