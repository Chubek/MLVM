#include <mlvm/sched.hpp>
#include <charconv>
#include <bit>

namespace mlvm::sched {
namespace {
namespace f = firestone::ir;
bool diagnostics(const f::Diagnostics& errors, DiagnosticEngine& d) {
    for (const auto& error : errors) d.error("firestone." + error.code, error.message);
    return errors.empty();
}
const std::map<std::string, f::Opcode, std::less<>> opcodes{
    {"addi", f::Opcode::Add}, {"add", f::Opcode::Add}, {"subi", f::Opcode::Sub}, {"sub", f::Opcode::Sub},
    {"muli", f::Opcode::Mul}, {"mul", f::Opcode::Mul}, {"divsi", f::Opcode::Div}, {"div", f::Opcode::Div},
    {"and", f::Opcode::And}, {"or", f::Opcode::Or}, {"xor", f::Opcode::Xor}};
std::string name(f::Opcode opcode) {
    switch (opcode) {
    case f::Opcode::Add: return "arith.addi"; case f::Opcode::Sub: return "arith.subi";
    case f::Opcode::Mul: return "arith.muli"; case f::Opcode::Div: return "arith.divsi";
    case f::Opcode::And: return "arith.and"; case f::Opcode::Or: return "arith.or";
    case f::Opcode::Xor: return "arith.xor";
    default: throw std::runtime_error("Firestone operator has no MLVM arithmetic lowering");
    }
}
f::Type type(std::string_view spelling) {
    unsigned bits = 0;
    if (spelling.size() < 2 || spelling[0] != 'i') throw std::runtime_error("dataflow lowering requires i1..i64 types");
    auto r = std::from_chars(spelling.data() + 1, spelling.data() + spelling.size(), bits);
    if (r.ec != std::errc{} || r.ptr != spelling.data() + spelling.size() || bits == 0 || bits > 64)
        throw std::runtime_error("dataflow lowering requires i1..i64 types");
    return f::Type::integer(bits);
}
}
bool verify_artifact(const Artifact& artifact, DiagnosticEngine& d) {
    try {
        if (artifact.kind == "mlvm") return verify(artifact.get<Module>(), d);
        if (artifact.kind == "firestone.dataflow") {
            const auto& data = artifact.get<Dataflow>(); bool ok = diagnostics(data.graph.verify(), d);
            if (data.roots.empty()) { d.error("firestone.roots", "dataflow artifact requires at least one root"); ok = false; }
            for (auto root : data.roots) if (!data.graph.graph().contains(root)) { d.error("firestone.root", "dataflow output references a missing node"); ok = false; }
            return ok;
        }
        if (artifact.kind == "firestone.3ac") return diagnostics(artifact.get<f::Typed3AC>().verify(), d);
        if (artifact.kind == "firestone.ssa") return diagnostics(artifact.get<f::SimpleSSAGraph>().verify(), d);
        if (artifact.kind == "text") { artifact.get<std::string>(); return true; }
        if (artifact.kind == "bytes") { artifact.get<std::vector<std::uint8_t>>(); return true; }
        if (artifact.kind == "bundle") {
            for (const auto& item : artifact.get<std::vector<Artifact>>()) if (!verify_artifact(item, d)) return false;
            return true;
        }
        if (artifact.kind.empty() || !artifact.value.has_value()) { d.error("sched.artifact", "artifact requires a kind and a value"); return false; }
        // Custom host actions are responsible for their own artifact verifier.
        return true;
    } catch (const std::exception& e) { d.error("sched.artifact", e.what()); return false; }
}
std::optional<Dataflow> to_dataflow(const Module& module, DiagnosticEngine& d) {
    if (!verify(module, d)) return {};
    try {
        const Block* block = nullptr;
        if (module.body.blocks.size() == 1 && module.body.blocks[0].operations.size() == 1) {
            const auto& function = module.body.blocks[0].operations[0];
            if (function.name == "builtin.func" && function.regions.size() == 1 && function.regions[0].blocks.size() == 1)
                block = &function.regions[0].blocks[0];
        }
        if (!block) throw std::runtime_error("dataflow lowering requires one single-block builtin.func");
        Dataflow out; std::map<ValueId, f::NodeId> values;
        auto value_type = [&](ValueId id) { return type(module.context->spelling(module.value(id)->type)); };
        for (auto arg : block->arguments) values[arg] = out.graph.input("arg" + std::to_string(arg), value_type(arg));
        for (const auto& op : block->operations) {
            if (op.name == "func.return") {
                for (auto operand : op.operands) out.roots.push_back(values.at(operand));
                continue;
            }
            if (op.results.size() != 1 || !op.regions.empty()) throw std::runtime_error("unsupported dataflow operation: " + op.name);
            auto prefix = op.name.substr(0, op.name.find('.')), suffix = op.name.substr(op.name.find('.') + 1);
            if (prefix != "arith" && prefix != "high" && prefix != "mid" && prefix != "low" && prefix != "machine" &&
                prefix != "interp" && prefix != "wasm" && prefix != "llvmir" && prefix != "gpu")
                throw std::runtime_error("operation has no Firestone dataflow semantics: " + op.name);
            for (const auto& [key, value] : op.attributes) {
                (void)value;
                if (key != "value" && key != "origin") throw std::runtime_error("cannot discard semantic attribute '" + key + "' during dataflow lowering");
            }
            auto id = op.results[0]; auto t = value_type(id);
            if (suffix == "constant") {
                auto s = op.attributes.at("value"); std::int64_t value = 0;
                auto r = std::from_chars(s.data(), s.data() + s.size(), value);
                if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) throw std::runtime_error("invalid dataflow integer constant");
                if (t.bits > 1 && t.bits < 64 && value >= (std::int64_t{1} << (t.bits - 1)))
                    value = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(value) | (~std::uint64_t{} << t.bits));
                values[id] = out.graph.constant(value, t);
            } else {
                auto it = opcodes.find(suffix);
                if (it == opcodes.end()) throw std::runtime_error("unsupported dataflow operation: " + op.name);
                std::vector<f::NodeId> operands;
                for (auto operand : op.operands) operands.push_back(values.at(operand));
                values[id] = out.graph.operation(it->second, t, operands);
            }
        }
        if (!verify_artifact({"firestone.dataflow", out}, d)) return {};
        return out;
    } catch (const std::exception& e) { d.error("sched.firestone", e.what()); return {}; }
}
std::optional<Module> from_dataflow(const Dataflow& data, Context& context, DiagnosticEngine& d) {
    if (!verify_artifact({"firestone.dataflow", data}, d)) return {};
    try {
        if (data.roots.size() != 1 || !data.is_signed) throw std::runtime_error("MLVM lowering requires a single signed dataflow result");
        Module out(context); auto& top = out.add_block(out.body);
        auto& function = out.append(top, "builtin.func"); function.attributes["sym"] = "pipeline";
        auto type = [&](f::NodeId id) {
            const auto& t = data.graph.node(id).type;
            if (t.kind != f::TypeKind::Integer || !t.bits || t.bits > 64) throw std::runtime_error("unsupported MLVM dataflow type");
            return context.intern("i" + std::to_string(t.bits));
        };
        function.attributes["result"] = context.spelling(type(data.roots[0])); function.regions.emplace_back();
        std::vector<Identifier> types; std::vector<f::NodeId> args;
        for (auto id : data.graph.schedule()) {
            const auto& inst = data.graph.node(id);
            if (inst.opcode == f::Opcode::Constant && inst.operands[0].kind == f::OperandKind::Symbol) { args.push_back(id); types.push_back(type(id)); }
        }
        auto& block = out.add_block(function.regions[0], types); std::map<f::NodeId, ValueId> values;
        for (std::size_t i = 0; i < args.size(); ++i) values[args[i]] = block.arguments[i];
        for (auto id : data.graph.schedule()) {
            if (values.contains(id)) continue;
            const auto& inst = data.graph.node(id); auto t = type(id);
            if (inst.opcode == f::Opcode::Constant) {
                auto& op = out.append(block, "arith.constant", {}, {&t, 1});
                op.attributes["value"] = std::to_string(inst.operands[0].integer); values[id] = op.results[0];
            } else {
                std::vector<ValueId> operands;
                for (const auto& arg : inst.operands) operands.push_back(values.at(arg.value));
                values[id] = out.append(block, name(inst.opcode), operands, {&t, 1}).results[0];
            }
        }
        auto root = values.at(data.roots[0]); out.append(block, "func.return", {&root, 1});
        if (!verify(out, d)) return {};
        return out;
    } catch (const std::exception& e) { d.error("sched.firestone", e.what()); return {}; }
}
firestone::ir::Typed3AC to_three_address(const Dataflow& data) {
    f::Typed3AC out; std::map<f::NodeId, f::ValueId> values;
    for (auto id : data.graph.schedule()) {
        const auto& inst = data.graph.node(id);
        if (inst.opcode == f::Opcode::Constant && inst.operands[0].kind == f::OperandKind::Symbol) values[id] = out.create_register(inst.type, true);
        else {
            auto operands = inst.operands;
            for (auto& arg : operands) if (arg.kind == f::OperandKind::Value) arg.value = values.at(arg.value);
            values[id] = out.compute(inst.opcode, inst.type, operands, inst.annotation);
        }
    }
    if (data.roots.size() != 1) throw std::runtime_error("3AC lowering requires one return value");
    out.emit({f::Opcode::Return, {}, {}, {f::Operand::ref(values.at(data.roots[0]))}, {}, {}, {}}); return out;
}
firestone::ir::SimpleSSAGraph to_ssa(const Dataflow& data) {
    f::SimpleSSAGraph out; auto block = out.cfg().create_block("entry"); std::map<f::NodeId, f::ValueId> values;
    for (auto id : data.graph.schedule()) {
        const auto& inst = data.graph.node(id);
        if (inst.opcode == f::Opcode::Constant && inst.operands[0].kind == f::OperandKind::Symbol) values[id] = out.parameter(inst.type);
        else {
            auto operands = inst.operands;
            for (auto& arg : operands) if (arg.kind == f::OperandKind::Value) arg.value = values.at(arg.value);
            values[id] = out.define(block, inst.opcode, inst.type, operands, inst.annotation);
        }
    }
    if (data.roots.size() != 1) throw std::runtime_error("SSA lowering requires one return value");
    out.cfg().return_value(block, f::Operand::ref(values.at(data.roots[0]))); return out;
}
} // namespace mlvm::sched
