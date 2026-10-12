# Chapter 5 — Kernels and inheritance

Every `.bst` file begins with one kernel declaration. Declarations of concerns
and capabilities follow, then rule declarations. A kernel may have no rules, which
is useful for composing inherited kernels.

```bst
Kernel ExampleLeft(inherits ExampleBase);
```

The declared name identifies the dependency; the filename does not. The kernel
header accepts a comma-separated list of inherited qualified names. Within a
resolved graph, bases execute before the current kernel, in the listed order.

## Loading dependencies

The CLI loads additional source directories when `--kernel-dir` is supplied:

```sh
python3 -m blackstone apply examples/kernels/inheritance/Diamond.bst \
  examples/add-zero-right.json --kernel-dir examples/kernels/inheritance --trace
```

Directories are searched recursively for `.bst` files. Repeating the option adds
more directories. The CLI diagnoses duplicate declared names rather than choosing
a file arbitrarily. Python integrations provide an explicit name-to-Kernel map to
`resolve`; C integrations provide the kernel-descriptor array to compilation.

## The inheritance diamond

The example directory contains this graph:

```text
ExampleDiamond
    ├── ExampleLeft ──┐
    └── ExampleRight ─┴── ExampleBase ── ConstantFolding
```

Resolution visits each kernel once. The combined rule order is:

```text
ConstantFolding
BaseAddZeroRight
LocalSubtractZero
```

`ExampleLeft` and `ExampleRight` add no local rules. Their shared ancestor is not
duplicated. For the provided addition fixture, the base's `BaseAddZeroRight`
applies; the local subtraction rule does not.

## Scope and duplicate rules

Each kernel declares the concerns and capabilities needed by its own local rules.
An inherited rule is validated against the declarations of the kernel that
contains it. Inheritance combines ordered rules; it is not an implicit import of
every base declaration into the derived source's local predicate namespace.

Rule names must be unique across the whole resolved inheritance graph. There is
no override syntax or implicit replacement of a base rule. Distinct unrelated
kernel graphs can each contain a rule with the same name, provided those graphs
are not combined into one resolved program.

Unknown bases and inheritance cycles are compile-time diagnostics. A cycle cannot
be repaired by a rewrite budget because it prevents a program from being resolved.

## Built-in folding

When `ConstantFolding` is inherited and no supplied kernel declares that name,
Blackstone inserts its built-in folding entry. Supplying a kernel with that name
uses the supplied definition instead. Folding does not occur merely because a
kernel contains integer operators; it must be present in the resolved rule list.

**Exercise:** run `check examples/kernels` and compare its directory-wide counts
with the three resolved entries in the diamond. Catalog inventory and one root's
resolved execution order answer different questions.

[Previous](04-lexical-syntax.md) · [Contents](README.md) · [Next: Concerns and HIR views](06-concerns-and-hir-views.md)
