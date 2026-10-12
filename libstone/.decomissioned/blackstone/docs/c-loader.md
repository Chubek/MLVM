# Native Optilisp loader

`optilisp/loader/Optlilisp.c` implements a C11 reader and a local kernel
validator. `Optlilisp.h` exposes the AST, source locations, diagnostics,
field extraction and vocabulary checks. `Optlilisp.hpp` supplies a move-only
C++17 `optlilisp::Program` owner. The existing `Optlilisp` spelling is retained
in filenames and API names.

## Build and check

```sh
make loader       # builds optilisp/loader/Optlilisp.o
make test-loader  # C regressions, full kernel catalog, compiled C++ client
make test         # native checks and Python tests
```

The native checks require C11 and C++17 compilers. The Python database tooling
continues to require only Python's standard library.

## C use and ownership

```c
#include "Optlilisp.h"

int load(const char *path)
{
    OptlilispProgram program;
    OptlilispSink diagnostics;
    int failed;
    optlilisp_program_init(&program);
    optlilisp_sink_init(&diagnostics);
    failed = optlilisp_parse_file(path, &program, &diagnostics);
    if (!failed)
        failed = optlilisp_validate_program(&program, &diagnostics);
    /* Consume diagnostics here, before freeing program's filename storage.
       All AST pointers are borrowed from program. */
    optlilisp_sink_fini(&diagnostics);
    optlilisp_program_fini(&program);
    return failed;
}
```

Initialize a program before parsing. Parsing replaces its contents, including
on file-open failure. Diagnostics borrow filename storage from the program;
consume or copy them before freeing or reparsing that program. Parse and
validation return values report errors added by that call, while the sink
retains earlier diagnostics. Allocation failure terminates with `abort()`.

Strings retain their decoded byte length in `text_length`, including embedded
NUL from `\0`. Use that length rather than `strlen` when consuming string
values. Source buffers passed to `optlilisp_parse` are NUL-terminated; raw NUL
bytes in files are rejected. Numeric integers must fit `long long`, unlike
Python's arbitrary-precision integers. Numeric conversion uses the C locale;
keep `LC_NUMERIC=C`. Lists are limited to 256 nesting levels and incomplete
lists are not published after fatal syntax errors.

`OptlilispKernelInfo` provides fixed-size convenience fields. Extraction fails
instead of truncating long fields or embedded-NUL metadata. Full values remain
available through AST accessors.

## C++ use

```cpp
#include "Optlilisp.hpp"

auto program = optlilisp::Program::parse_file("kernel.olisp");
auto kernel = optlilisp::kernel_info(program.form(0));
```

Link the client against `Optlilisp.o` compiled as C. Parse failures throw
`optlilisp::Error`. Pass a `std::vector<optlilisp::Diagnostic>*` to retain owned
diagnostic strings after an exception. Form pointers remain valid while their
program owner exists.

## Validation boundary

The reader supports lists, symbols, keywords, strings and the documented
escapes, integers, floats, booleans, nil and line comments. Local validation
checks required nonempty sections, keyword types, duplicate and unknown kernel
sections, selected section shapes, stage/verb compatibility and transformation
arity. Registry definitions remain accessible as generic AST forms.

This is a subset of database validation. Reference resolution, registry
validation, dependency checks, analysis contracts and JSON Schema checks are
performed by `tools/validate-kernels`. Native success does not establish
transformation correctness or full database validity.
