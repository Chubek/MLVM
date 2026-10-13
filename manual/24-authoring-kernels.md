# 24. Authoring a Blackstone kernel

[Contents](README.md) · [Next: Contracts](25-kernel-contracts.md)

Save this complete definition as `AddZero.bst`:

```text
Kernel AddZero;
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
requires Arithmetic::Domains::ModularInteger;

AddZeroRight :: Binary(Term!resolves, Term!exact(0)) -> Term
where ModularInteger {
    $1 `plus` $2 ~> $1;
}
```

Term leaves are numbered depth-first from `$1`. The match expression must have
exactly the signature's tree and capture positions. `->` declares the result
sort; `~>` separates the match from its directed replacement. Backtick operators
have equal precedence and associate left-to-right; use parentheses for trees.

Refinements are `resolves`, `literal`, `nonliteral`, `exact(integer)` and
`same($earlier_capture)`. The latter compares stable value identity. Identical
text or equal-looking subtrees do not establish identity. Decimal spellings
remain strings until the typed adapter checks representability.

Guards are registered predicates joined with `&&`. Every predicate must be
proven true. Erasing a captured computation generally requires `Pure($n)` and
`Total($n)`; the zero-addition rule retains its captured value and needs neither.

Schedule the file using:

```lua
local p = require("lsched").pipeline()
local input = p:ir("input", {path="input.mlvm"})
p:kernel("optimized", input, {path="AddZero.bst"})
```

Inheritance uses `Kernel MyKernel(inherits Base, OtherBase);`. Supply dependency
files through the kernel action's `directory` option. Native resolution detects
cycles and duplicate rule names, and establishes deterministic base/source order.

The complete language contract and examples are in
[Blackstone's manual](../libstone/blackstone/manual/README.md).
