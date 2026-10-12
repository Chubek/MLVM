# Chapter 3 — Your first rewrite

[`examples/kernels/Identity.bst`](../examples/kernels/Identity.bst) is a complete
kernel that forwards the left operand of an addition by zero:

```bst
Kernel ExampleIdentity;

concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

KeepLeft :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger
{
    $1 `plus` $2 ~> $1;
}
```

Read the rule in five parts:

1. `KeepLeft` is the rule name, used in diagnostics and traces.
2. The signature requires a Binary with two Term leaves: a resolved value and
   a concrete typed zero. The leaves bind `$1` and `$2`.
3. `-> Term` declares the replacement sort, not a new arithmetic type.
4. `where ModularInteger` requires the matched binary and its operands to have
   the same scalar, wrapping integer type and eligible semantic flags.
5. The body recognizes the registered `plus` opcode and returns capture `$1`.

## Parse, check and apply

With chapter 2's environment:

```sh
python3 -m blackstone parse examples/kernels/Identity.bst
python3 -m blackstone check examples/kernels/Identity.bst
python3 -m blackstone apply examples/kernels/Identity.bst \
  examples/add-zero-right.json --trace
```

`parse` prints the AST with source spans. `check` resolves the declarations and
validates the rule. `apply` optimizes a typed HIR fixture. Add `--engine native`
to run the same source through the C profile compiler and matcher.

## Why the input is effectful

The fixture marks its input `effectful_x` as `pure: false`, `total: true`.
The identity still applies because the replacement retains exactly one occurrence
of that input. The original expression evaluates `effectful_x` once and the zero
literal once; the replacement retains the observable input evaluation.

Contrast this with `x times 0 -> 0`: that replacement discards an evaluation of
`x`, so the acceptance kernel requires both `Pure` and `Total`. Chapter 14 develops
this distinction in detail.

## Read the output

The optimized root is an unsigned-8-bit input whose descriptive name is
`effectful_x`. Output IDs are deterministic serializer IDs such as `n0`; their
spelling need not match the original JSON IDs. The capture keeps its identity and
provenance. The trace names `KeepLeft`, even though forwarding an existing capture
does not overwrite that capture's `origin` field.

**Exercise:** apply this kernel to `examples/mixed-types.json`. The expression
remains unchanged because the zero is 8-bit and the other operand/result are
16-bit. A syntactic zero match does not establish a valid typed identity by itself.

[Previous](02-build-and-tooling.md) · [Contents](README.md) · [Next: Lexical syntax](04-lexical-syntax.md)
