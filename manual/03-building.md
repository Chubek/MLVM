# 3. Configuring and building

[Contents](README.md) · [Next: Installation](04-installation.md)

The standalone build requires CMake 3.20 or newer and a C++20 toolchain. C
compilation is also enabled. Run from the MLVM directory:

```sh
cmake -S . -B build -DMLVM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Selecting components

| Variable | Default | Meaning |
| --- | --- | --- |
| `MLVM_BUILD_TOOLS` | `ON` | Build command-line executables |
| `MLVM_BUILD_TESTS` | `ON` | Build and register tests |
| `MLVM_ENABLE_WARNINGS` | `ON` | Enable strict compiler warnings |
| `MLVM_ENABLE_CEPHYR` | `AUTO` | Optional cparser-iso11 C11 frontend |
| `MLVM_ENABLE_BINUTILS` | `AUTO` | Optional library-backed binary tools |
| `MLVM_ENABLE_OPTILISP` | `OFF` | Legacy Optilisp integration, when its sources are separately available |

The optional features accept `AUTO`, `ON` and `OFF`. `AUTO` detects available
sources or runtimes; `ON` makes missing prerequisites a configuration error.
Use `ON` in a build intended to demonstrate a specific optional feature so that
its absence cannot be mistaken for a successful feature build.

For the scheduler/core build with optional frontend and binary adapters disabled:

```sh
cmake -S . -B build-minimal \
  -DMLVM_ENABLE_CEPHYR=OFF \
  -DMLVM_ENABLE_BINUTILS=OFF \
  -DMLVM_ENABLE_OPTILISP=OFF
cmake --build build-minimal --parallel
```

## Dependency locations

Cephyr needs `third_party/cparser-iso11`, `third_party/ucpp`, and Python 3 for
its build-local embedding adapters. `MLVM_CPARSER_SOURCE_DIR` overrides the
parser path and `MLVM_UCPP_SOURCE_DIR` the preprocessor path. Binary tools use the supplied
Bin2Bin, klib, and tomlc99 sources.

The scheduler is built by default and requires `third_party/lua`,
`third_party/kaguya`, `third_party/dparser`, `libstone/blackstone` and
`libstone/firestone`. `MLVM_LUA_SOURCE_DIR`, `MLVM_KAGUYA_SOURCE_DIR` and
`MLVM_DPARSER_SOURCE_DIR` override the third-party paths. Lua is compiled into
the library; Kaguya is header-only; Blackstone grammar tables are generated in
the build directory. No network fetch or runtime Python dependency is involved.

`MLVM_THIRD_PARTY_DIR` sets the shared dependency root. Read configuration
messages before assuming an optional tool is available.

Reference: [CMake build](../CMakeLists.txt), [scheduler dependencies](../cmake/MLVMScheduler.cmake).
