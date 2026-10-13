# Building libstone in MLVM

The five Stone components share MLVM's `third_party/` directory. CMake never
downloads dependencies. From the MLVM repository root:

```sh
./scripts/get-deps.sh
cmake -S . -B build -DMLVM_BUILD_LIBSTONE=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The normal MLVM build continues to use its focused Stone adapters.
`MLVM_BUILD_LIBSTONE=ON` additionally builds the complete Stone projects.
The aggregate and individual components can also be configured independently:

```sh
cmake -S libstone -B build-stone
cmake -S libstone/sandstone -B build-sandstone
cmake -S libstone/yellowstone/qbfd -B build-qbfd
```

## Options

- `LIBSTONE_BUILD_BLACKSTONE`, `LIBSTONE_BUILD_FIRESTONE`,
  `LIBSTONE_BUILD_LIMESTONE`, `LIBSTONE_BUILD_SANDSTONE`, and
  `LIBSTONE_BUILD_YELLOWSTONE` select components in the aggregate build.
- `LIBSTONE_BUILD_TESTS`, `LIBSTONE_BUILD_TOOLS`, and
  `LIBSTONE_ENABLE_WARNINGS` default to the corresponding MLVM settings when
  composed with MLVM. Standalone tests default to `BUILD_TESTING`.
- Each component has its own `*_BUILD_TESTS` option. Components with tools also
  have `*_BUILD_TOOLS`. Tests may build CLI executables as test helpers even
  when tool installation is disabled.
- `DOMTERM_BUILD_STDLIB=ON` builds the optional Termscript standard library
  using `third_party/tomlc99` and `third_party/isocline`.

Set `MLVM_THIRD_PARTY_DIR` on the first configure to relocate all dependency
defaults. Individual `MLVM_<NAME>_SOURCE_DIR` cache variables override specific
checkouts, including `MLVM_METATK_SOURCE_DIR`, `MLVM_DPARSER_SOURCE_DIR`,
`MLVM_LUA_SOURCE_DIR`, and `MLVM_KAGUYA_SOURCE_DIR`. When changing the root in
an existing build, clear the cached per-dependency paths or use a fresh build.

Sandstone and Yellowstone use `metatk/DSLtk/DSLtk.hpp`, replacing the old
DomDSL copy. VMWeave builds Lua from sources and exports that library with
Limestone; installed consumers do not need a separately installed Lua.
Satie and VMWeave's allocator tests use the compatible DomMEMTk snapshot under
`third_party/satie/third_party/memtkx` by default. The separate upstream
`memtkx` checkout has a renamed API. The `exolayer-manifest` check and the
`19_exolayer.md` install are enabled only when the configured ExolangTk
checkout provides them; upstream checkouts without
`manifests/Exolayer-Modules.yaml` skip that test instead of failing.

System build prerequisites include a C/C++ compiler and CMake 3.20 or newer.
Parser generation requires Perl. Python is needed for the Python test suites.
Sandstone requires zlib and optionally uses zstd. Limestone uses libffi and
LMDB unless `LIMESTONE_ENABLE_NATIVE_FFI` or
`LIMESTONE_ENABLE_PERSISTENT_CACHE`, respectively, is disabled. SWIG Python
bindings and D MachineIR tests retain their separate optional toolchains.

## Limestone dependency revisions

The complete Limestone backend needs the extended Equinox-NG and EkippX APIs
used by its sources: `set_observers`, `set_interrupt`, `add_node`, and
`ekippx::MappedExpansion`. The current upstream Git checkouts do not provide
all of these APIs. `get-deps.sh` verifies source files, not API compatibility.
Use compatible snapshots through explicit overrides. For example, with the
local legacy checkout used for validation:

```sh
legacy=/mnt/lambda/hearthstone/limestone/third_party
cmake -S libstone/limestone -B build-limestone \
  -DMLVM_EQUINOX_NG_SOURCE_DIR="$legacy/equinox-ng" \
  -DMLVM_METATK_SOURCE_DIR="$legacy/metatk"
cmake --build build-limestone --parallel
ctest --test-dir build-limestone --output-on-failure
```

The same overrides work with the aggregate and full MLVM builds. They are
explicit configuration inputs; no machine-specific lookup is built into CMake.

## Dependency script

`scripts/get-deps.sh` covers the root dependency inventory, including MetaTk,
plus the missing dependencies of the full Stone builds. Run `--help` for the
package list and options. It accepts `--dir`, selected packages, `--update`,
`--force`, and `--no-verify`, and initializes Git submodules recursively.
Existing source snapshots are verified and kept by default; updating one
requires replacing it with a Git checkout explicitly via `--force`.

The script's offline regression checks are available through CTest or directly:

```sh
python3 tests/integration/get_deps.py
```
