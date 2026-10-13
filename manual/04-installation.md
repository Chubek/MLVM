# 4. Installation and embedding setup

[Contents](README.md) · [Next: First IR workflow](05-first-ir.md)

Install a completed standalone build into an explicit prefix:

```sh
cmake --install build --prefix "$HOME/.local/mlvm"
```

The package exports CMake targets and public headers. A minimal C++ consumer
project can use this `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mlvm_client LANGUAGES CXX)
find_package(MLVM CONFIG REQUIRED)
add_executable(client main.cpp)
target_link_libraries(client PRIVATE MLVM::IR)
```

Configure the consumer from its own directory:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local/mlvm"
cmake --build build
```

## Choosing a library

| Target | Use |
| --- | --- |
| `MLVM::Core` | Contexts, diagnostics, C context ABI |
| `MLVM::IR` | SSA, parsing, verification, passes, conversions |
| `MLVM::Codegen` | Baseline target and instruction encoding |
| `MLVM::Object` | Baseline ELF, assembly, archives, linking |
| `MLVM::NodeIR` | Header-only stage-specific graphs |
| `MLVM::Binutils` | Optional broader binary-object APIs |
| `MLVM::Cephyr` | Optional frontend |
| `MLVM::Scheduler` | Lua DAG orchestration and native kernel/IR adapters |
| `MLVM::Blackstone` | Supplied native C kernel runtime |
| `MLVM::Firestone` | Supplied header-only IR representations |

Optional targets exist only when their component was enabled. Scheduler, Lua,
Blackstone and Firestone integration are included in the standard build.

Cephyr's private cparser-iso11 archive is installed/exported with its target.
Scheduler and binary-tool dependency archives are exported
with their respective package configurations.

## Kernel data after relocation

Installations include `share/mlvm/blackstone/kernels` and runnable orchestrators
in `share/mlvm/examples/sched`. Installed orchestrator paths are adjusted to the
installed catalog and remain relative to the script, supporting prefix relocation.
C++ callers supply kernel and script paths explicitly. Link `MLVM::Scheduler`
to use `<mlvm/sched.hpp>`; its exported target brings the runtime dependencies.

Reference: [public API contract](../docs/API.md).
