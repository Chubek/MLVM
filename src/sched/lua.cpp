#include <mlvm/sched.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
// Kaguya's optional allocator constructors use Lua's pre-5.5 signature.
#if LUA_VERSION_NUM >= 505
inline lua_State* lua_newstate(lua_Alloc allocate, void* user) { return ::lua_newstate(allocate, user, 0); }
#define LUA_GCSETPAUSE (-100)
#define LUA_GCSETSTEPMUL (-101)
inline int mlvm_lua_gc(lua_State* state, int operation, int value) {
    if (operation == LUA_GCSETPAUSE) return ::lua_gc(state, LUA_GCPARAM, LUA_GCPPAUSE, value);
    if (operation == LUA_GCSETSTEPMUL) return ::lua_gc(state, LUA_GCPARAM, LUA_GCPSTEPMUL, value);
    return ::lua_gc(state, operation, value);
}
#define lua_gc mlvm_lua_gc
#endif
#include <kaguya/kaguya.hpp>
#if LUA_VERSION_NUM >= 505
#undef lua_gc
#undef LUA_GCSETPAUSE
#undef LUA_GCSETSTEPMUL
#endif

namespace mlvm::sched {
namespace {
std::string string(const kaguya::LuaRef& ref) {
    if (ref.type() != LUA_TSTRING) throw std::invalid_argument("lsched expects a string");
    auto s = ref.get<std::string>();
    if (s.find('\0') != std::string::npos) throw std::invalid_argument("lsched strings cannot contain NUL");
    return s;
}
std::vector<std::string> list(const kaguya::LuaRef& ref) {
    if (ref.type() == LUA_TNIL) return {};
    if (ref.type() != LUA_TTABLE) throw std::invalid_argument("dependencies must be a dense array of node names");
    auto table = ref.get<kaguya::LuaTable>(); std::vector<std::string> out;
    auto keys = table.keys();
    for (std::size_t i = 1; i <= keys.size(); ++i) out.push_back(string(table[i]));
    for (const auto& key : keys) {
        if (key.type() != LUA_TNUMBER) throw std::invalid_argument("dependency array has a non-integer key");
        auto number = key.get<double>();
        if (number < 1 || number > keys.size() || number != static_cast<std::size_t>(number)) throw std::invalid_argument("dependency array is not dense");
    }
    return out;
}
}
std::optional<Graph> parse(std::string_view source, std::string_view name, const Registry& registry, DiagnosticEngine& d) {
    if (source.find('\0') != std::string_view::npos) { d.error("sched.lua", "orchestrator contains NUL"); return {}; }
    try {
        Graph graph; std::string error; kaguya::State lua;
        if (!lua.state()) throw std::runtime_error("cannot create Lua state");
        lua.setErrorHandler([&](int, const char* message) { error = message ? message : "Lua execution failed"; });
        lua["_lsched_node"] = kaguya::function([&](kaguya::LuaTable spec) {
            const std::set<std::string> allowed{"name", "action", "inputs", "after", "options"};
            for (const auto& key : spec.keys()) if (!allowed.contains(string(key))) throw std::invalid_argument("unknown lsched node field: " + string(key));
            Node node; node.name = string(spec["name"]); node.action = string(spec["action"]);
            node.inputs = list(spec["inputs"]); node.after = list(spec["after"]); Options options;
            kaguya::LuaRef opts = spec["options"];
            if (opts.type() != LUA_TNIL) {
                if (opts.type() != LUA_TTABLE) throw std::invalid_argument("node options must be a string-to-string table");
                auto table = opts.get<kaguya::LuaTable>();
                for (const auto& key : table.keys()) { auto k = string(key); options.emplace(k, string(table[k])); }
            }
            node.run = registry.make(node.action, options); auto result = node.name;
            DiagnosticEngine local;
            if (!graph.add(std::move(node), local)) throw std::invalid_argument(local.diagnostics().front().message);
            return result;
        });
        // The Lua facade is deliberately small: Lua builds a graph, C++ executes it.
        const char* library = R"lua(
local add = _lsched_node
_lsched_node = nil
local lib = { version = 1 }
function lib.node(spec) return add(spec) end
function lib.pipeline()
  local graph = {}
  function graph:node(spec) return lib.node(spec) end
  function graph:ir(name, options)
    return self:node {name=name, action='ir', options=options}
  end
  function graph:cephyr(name, options)
    return self:node {name=name, action='cephyr', options=options}
  end
  for _, action in ipairs {'kernel', 'pass', 'convert', 'verify', 'text', 'target'} do
    graph[action] = function(self, name, input, options)
      return self:node {name=name, action=action, inputs={input}, options=options}
    end
  end
  function graph:collect(name, inputs)
    return self:node {name=name, action='collect', inputs=inputs}
  end
  return graph
end
lsched = lib
package.loaded.lsched = lib
)lua";
        if (!lua.dostring(library)) throw std::runtime_error(error);
        auto* state = lua.state(); std::string chunk = "@" + std::string(name);
        if (luaL_loadbufferx(state, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
            const char* message = lua_tostring(state, -1); throw std::runtime_error(message ? message : "Lua execution failed");
        }
        if (!error.empty()) throw std::runtime_error(error);
        if (!graph.validate(d)) return {};
        return graph;
    } catch (const std::exception& e) { d.error("sched.lua", e.what()); return {}; }
}
} // namespace mlvm::sched
