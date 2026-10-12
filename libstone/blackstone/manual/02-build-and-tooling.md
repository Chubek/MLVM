# Chapter 2 — Building and running the tools

Blackstone requires a C compiler, CMake 3.20 or newer, and Python 3.9 or newer.
The Python implementation uses the standard library. dparser sources are supplied
under `../../third_party/dparser` relative to the Blackstone directory.

## Build from Blackstone

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The build creates:

| Artifact | Purpose |
| --- | --- |
| `build/blackstone-parser` | Native `.bst` parser, emitting a concrete syntax tree |
| `build/libblackstone.so` | C11 registry, compiler, matcher and rewrite runtime |
| `build/blackstone-native-example` | Public-API profile compilation and application example |
| `build/blackstone-native-test` | Direct native API contract checks when testing is enabled |

Grammar tables are generated in the build directory. The build explicitly uses
dparser's supplied `write_tables.c`; there is no generated table to edit in the
Blackstone source directory.

## Select the parser, library and Python package

```sh
export BLACKSTONE_PARSER="$PWD/build/blackstone-parser"
export BLACKSTONE_LIBRARY="$PWD/build/libblackstone.so"
export PYTHONPATH="$PWD/python"
```

Use the platform-specific shared-library name on systems that produce a `.dylib`
or `.dll`. Without explicit environment variables, the Python frontend and native
bridge search Blackstone's local `build` directory. An external build directory
therefore needs explicit tool paths.

To select an alternate supplied dparser directory:

```sh
cmake -S . -B build -DBLACKSTONE_DPARSER_DIR=/path/to/dparser
```

## Check the setup

```sh
python3 -m blackstone check kernels/expressions/AlgebraicSimplification.bst
python3 -m blackstone apply kernels/expressions/AlgebraicSimplification.bst \
  examples/reassociate.json --engine native --trace
python3 examples/run_examples.py --engine both
./build/blackstone-native-example
```

The first command reports 16 local and 17 resolved rules. The application emits
optimized HIR JSON on stdout and trace events on stderr. The collection runner
checks all 27 documented cases against both drivers, including expected budget
failures. The C example compiles and reloads a one-rule profile before returning
the original input handle from an addition-by-zero match.

## Installation and debugging builds

`cmake --install build --prefix /chosen/prefix` installs the native library,
parser executable and public headers. For an installed parser/library, set the
environment variables to their installed locations; the source Python package
still needs to be on `PYTHONPATH` in this development workflow.

The C API can be checked with AddressSanitizer and UndefinedBehaviorSanitizer as
shown in chapter 23. Keep generated files in build directories so that changing
the grammar or compiler configuration produces a clean, reproducible rebuild.

[Previous](01-introduction.md) · [Contents](README.md) · [Next: Your first rewrite](03-first-rewrite.md)
