#include <mlvm/sched.hpp>
#include <iostream>

int main() {
    mlvm::Context context; mlvm::DiagnosticEngine diagnostics;
    mlvm::register_builtin_dialects(context, diagnostics);
    auto registry = mlvm::sched::Registry::builtins(context);
    auto graph = mlvm::sched::parse(R"(
local p = require('lsched').pipeline()
local input = p:ir('input', {source=[[
mlvm 1 { block() {
  "builtin.func"() ["sym"="answer", "result"="i32"] region { block() {
    %r:"i32" = "arith.constant"() ["value"="42"];
    "func.return"(%r) [];
  } };
} }
]]})
p:target('answer', input, {triple='interp-unknown-unknown'})
)", "consumer.sched", registry, diagnostics);
    mlvm::sched::Result result;
    if (!graph || !mlvm::sched::Scheduler{}.run(*graph, result, diagnostics)) {
        for (const auto& diagnostic : diagnostics.diagnostics()) std::cerr << diagnostic.message << '\n';
        return 1;
    }
    return result.artifacts.at("answer").get<std::string>() == "42\n" ? 0 : 1;
}
