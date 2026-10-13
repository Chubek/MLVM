# 41. Lua schedulers and DAG orchestration

[Contents](README.md) · [Previous: Extending MLVM](40-extending.md)

An orchestrator is a Lua program with the **`.sched`** extension. MLVM embeds
the supplied `third_party/lua` and binds the `lsched` library through
`third_party/kaguya`. Loading executes Lua to construct a pipeline; running the
pipeline invokes native C++ actions and Blackstone's native rewrite driver.
No Python interpreter or external Blackstone executable is needed at runtime.

## First pipeline

```lua
local p = require("lsched").pipeline()
local input = p:ir("input", {path = "input.mlvm"})
local optimized = p:kernel("optimized", input, {
  path = "AlgebraicSimplification.bst",
  budget = "10000",
})
local graph = p:convert("graph", optimized, {to = "firestone.dataflow"})
local linear = p:convert("linear", graph, {to = "firestone.3ac"})
local ssa = p:convert("ssa", graph, {to = "firestone.ssa"})
p:collect("outputs", {linear, ssa})
return p
```

Each convenience method returns the new node's **name**, which is used as an
input reference. `pipeline()` is a Lua builder facade for the script's single
native graph. Multiple builder objects contribute to that same graph. Returning
the builder is optional. Ordinary Lua functions, loops and conditionals may
generate nodes; node names must be unique. Prefer ordered arrays and `ipairs`
when declaration order matters.

Runnable source-tree examples:

```sh
build/mlvm-sched examples/sched/algebra.sched --trace
build/mlvm examples/sched/firestone.sched
build/mlvm --schedule=examples/sched/algebra.sched
build/mlvm-sched examples/sched/dependencies.sched --dot
build/mlvm-sched examples/sched/targets.sched --result=wasm -o build/pipeline.wasm
```

## Nodes, dependencies and execution

The general declaration is available as either `lsched.node(spec)` or
`p:node(spec)`:

```lua
p:node {
  name = "checked", action = "verify",
  inputs = {"optimized"},
  after = {"other_branch"},
  options = {},
}
```

* `name` and `action` are required nonempty strings.
* `inputs` is an ordered, dense array of predecessor names. Each predecessor's
  immutable artifact becomes an action argument, in exactly that order.
* `after` is a dense array of ordering-only predecessors.
* `options` is a string-to-string table. Unknown fields and options are errors.
  Numeric settings, including `budget`, are decimal **strings**.

Forward references are resolved after the Lua chunk finishes. The native graph
uses Firestone's `RawDAG`; unknown dependencies, self-edges and cycles are errors
before any action runs. A repeated dependency creates only one scheduling edge,
while repeated `inputs` entries still supply repeated arguments.

Execution is synchronous and deterministic: ready nodes run in declaration order,
and each node executes once per run. Fan-out creates independently owned output
artifacts. Fan-in is explicit; `collect` creates an ordered bundle rather than
silently merging different IRs. Independent branches are not executed on worker
threads. Kernel fixed-point iteration happens **inside** a node and is bounded
by that node's rewrite budget; the pipeline itself remains acyclic.

Every supported IR artifact is verified before publication. On a missing input,
action exception, kernel failure or invalid IR, execution stops. `Scheduler::run`
leaves the caller's entire previous `Result` untouched. Successful intermediate
artifacts exist only in the candidate run. Lua programs and custom callbacks
have normal host side effects; artifact rollback does not undo those effects.

## Built-in actions

| Action | Inputs | Options and output |
| --- | --- | --- |
| `ir` | none | Exactly one of `path` or `source`; parses verified MLVM text into `mlvm` |
| `cephyr` | none | Exactly one of `path` or `source`; lowers one C translation unit into `mlvm` (Cephyr-enabled builds only; see below) |
| `kernel` | one `mlvm` or `firestone.dataflow` | `path` to `.bst` or `.bsp`; optional `directory` for recursive inheritance lookup, `budget` (default `10000`); returns the same artifact kind |
| `pass` | one `mlvm` | `name`: `constant-fold`, `canonicalize`, or `dce` |
| `convert` | one IR | `to`: a supported representation or dialect, described below |
| `verify` | one artifact | No options; validates and forwards an owned copy |
| `collect` | one or more artifacts | No options; returns an ordered `bundle` |
| `text` | one printable artifact | No options; returns `text` |
| `target` | one `mlvm` | `triple`; interpreter additionally accepts comma-separated unsigned decimal bit-pattern `arguments` |

Convenience signatures are `p:ir(name, options)`, `p:cephyr(name, options)`,
`p:kernel/pass/convert/verify/text/target(name, input, options)`, and
`p:collect(name, inputs)`. File options resolve relative to the `.sched` file,
independent of the command's working directory. For C++ `Scheduler::run`, the
caller supplies that base directory explicitly. `p:cephyr` requires an action
named `cephyr` in the registry; `mlvm-sched` and `mlvm --schedule` register
Cephyr's action when built with Cephyr enabled, and loading a pipeline that
uses it otherwise fails with an unknown-action error.

## Firestone representations

The built-in routes are:

```text
MLVM -> Firestone DataflowGraph -> Typed3AC
                              -> SimpleSSAGraph
                              -> MLVM
```

`mlvm::sched::Dataflow` owns an actual Firestone `DataflowGraph`, an ordered list
of output root IDs, and signedness. The bridge accepts one single-block
`builtin.func` with integer parameters, constants, the seven modular arithmetic
operators, and a value return. It rejects effects, extra semantic attributes,
unsupported operations and unsupported types. `Typed3AC`, SSA and MLVM output
currently require one result. Unsigned dataflow can be rewritten natively, but
the generic MLVM round trip requires signed interpretation. Firestone literal
storage is `int64_t`; a constructed value outside that range is diagnosed.
Firestone's `i1` Boolean type is excluded from the modular-integer rewrite domain.

Original definitions remain allocated after a rewrite, including potentially
trapping division. Outputs and SSA uses refer to the normalized values. An
explicit host pass decides whether dead definitions may be removed.

Other Firestone representations can be carried as owning `Artifact` values by
custom C++ actions. Such actions supply their own verifier; the built-in verifier
knows `mlvm`, `firestone.dataflow`, `firestone.3ac`, and `firestone.ssa`.

## Dialect and target routes

`convert` also accepts `high`, `mid`, `low`, `machine`, `interp`, `llvmir`,
`wasm`, and `gpu`. The standard expression route is
`arith -> high -> mid -> low -> target-facing dialect`. Conversion is
transactional and fails when an operation lacks an explicit lowering.
See [the dialect inventory](../dialects/README.md).

| Target triple | Output | Implemented contract |
| --- | --- | --- |
| `x86_64-unknown-linux-gnu` | ELF64 object bytes | Single-block i32 arithmetic functions, up to six SysV integer parameters; C-dialect modules via the C backend |
| `wasm32-unknown-unknown` | WebAssembly module bytes | i32/i64 arithmetic functions and named exports |
| `llvmir-unknown-unknown` | LLVM IR text | i1..i64 integer SSA functions; consumer supplies the target layout |
| `interp-unknown-unknown` | Decimal result text | One integer function; i1..i64 wrapping arithmetic, checked signed division |

Memory, structured regions, GPU launch and interpreter deoptimization operations
have dialect verification contracts and require host-specific lowering for these
expression targets. Unsupported semantics produce diagnostics.

## C pipelines

The `cephyr` source action compiles one C translation unit per node through the
same preprocess, parse and lower stages as the `cephyr` tool, then publishes a
verified `mlvm` artifact holding `builtin`, `arith`, `func` and `c`-dialect
operations. Downstream `kernel`, `pass`, `verify`, `text` and `collect` nodes
work unchanged; C functions that lower to pure integer expressions additionally
flow through `convert` and every `target` route. Modules using C memory,
address, call or control operations take the x86-64 `target` route, which emits
them through the C backend to ELF64 object bytes:

```lua
local p = require("lsched").pipeline()
local src = p:cephyr("src", {path = "answer.c"})
local obj = p:target("obj", src, {triple = "x86_64-unknown-linux-gnu"})
p:text("ir", src)
return p
```

| Option | Meaning |
| --- | --- |
| `path` / `source` | Exactly one: a C file relative to the `.sched` file, or inline C text |
| `preprocessor` | `internal`, `ucpp` (default), or `system` |
| `system_preprocessor` | Command for the `system` engine (default auto-detection) |
| `sysroot` | Headers/runtime directory relative to the `.sched` file (default bundled sysroot; empty clears it) |
| `includes` | Comma-separated `-I` directories relative to the `.sched` file |
| `defines` | Comma-separated `NAME[=VALUE]` macros |

Option validation happens when the pipeline loads; preprocessing, semantic and
lowering diagnostics name the pipeline's file or inline source. Only the entry
C file is tracked as a read dependency; headers pulled in during preprocessing
are not. Truncated control flow, calls and other C operations have no
expression lowering, so `convert` and non-x86 `target` routes fail explicitly
for modules that use them. See [the Cephyr reference](../docs/CEPHYR.md).

## CLI controls

* `--check` loads Lua and validates the graph without running actions.
* `--dot` prints the scheduling DAG without running actions.
* `--trace` prints completed actions and applied Blackstone rule names to stderr.
* `--result=NODE` selects an artifact to publish. By default it is the last node
  in topological order. The full graph executes even when selecting an earlier node.
* `-o FILE` writes the selected artifact. Text defaults to stdout; binary output
  requires a file. Output is written only after a successful complete run, and
  cannot overwrite the orchestrator or the built-in actions' file dependencies.

## C++ embedding

Link `MLVM::Scheduler` and include `<mlvm/sched.hpp>`:

```cpp
mlvm::Context context;
mlvm::DiagnosticEngine diagnostics;
mlvm::register_builtin_dialects(context, diagnostics);
auto registry = mlvm::sched::Registry::builtins(context);
auto graph = mlvm::sched::load("pipeline.sched", registry, diagnostics);
mlvm::sched::Result result;
if (graph && mlvm::sched::Scheduler{}.run(*graph, result, diagnostics, ".")) {
    // result.artifacts.at("node-name") and result.events are now published.
}
```

The context must outlive the registry, graph and all artifacts borrowing it.
`Registry::add(name, factory)` adds native actions callable by `.sched` files.
An `ActionFactory` validates its options at load time and returns an `Action`;
the action receives immutable input pointers and a `RunContext`, and returns
`optional<Artifact>`. Return an owning copy and verify custom representations
before returning. Never mutate an input through shared pointers stored in
`std::any`: ownership isolation is part of the custom-action contract.

Implementation: [`include/mlvm/sched.hpp`](../include/mlvm/sched.hpp),
[`src/sched`](../src/sched), [`examples/sched`](../examples/sched).
