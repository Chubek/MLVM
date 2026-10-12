# Chapter 4 — Lexical syntax and source locations

Blackstone identifiers and keywords are case-sensitive. An identifier uses ASCII
letters or underscore followed by ASCII letters, decimal digits or underscore:

```text
[A-Za-z_][A-Za-z0-9_]*
```

`Kernel`, `concerns`, `requires`, `inherits` and `where` have their specified case.
`Term` and `Binary` are the language's two sorts. Registered operator and predicate
names are also case-sensitive; `plus` and `Plus` are different names.

## Whitespace and comments

Whitespace can separate any tokens. Newlines have no statement-ending meaning,
so signatures and guards can span multiple lines. Blackstone supports:

```text
// a line comment, ending at the next newline
/* a block comment, ending at the first closing delimiter */
```

Block comments do not nest. Unterminated block comments and backticks produce
source-located diagnostics. Embedded NUL bytes are rejected by the frontend.
Comments can even separate the minus sign and digits of a negative integer;
the AST records the resulting integer spelling rather than the intervening text.

## Punctuation and integer spellings

| Token | Meaning |
| --- | --- |
| `::` | Qualified-name separator or rule-signature separator |
| `->` | Replacement sort declaration |
| `~>` | Directed match-to-replacement separator |
| `!` | Term refinement |
| `$` followed by decimal digits | Capture reference |
| `&&` | Guard conjunction |
| Backticks around an identifier | Registered infix operator |
| `;` | Declaration or rewrite terminator |

Integers are decimal digits with an optional leading `-`. There are no hexadecimal
integer tokens, quoted strings or embedded host-language expressions in the v0
source grammar. Integer spellings are preserved without machine-word overflow;
the matched or constructed type decides representability later. Captures are
one-based, and `$0` is rejected during validation.

## Infix grouping

All backtick operators have equal precedence and associate left-to-right.
Mathematical precedence is not inferred from names:

```text
$1 `plus` $2 `times` $3     means ($1 `plus` $2) `times` $3
$1 `plus` ($2 `times` $3)   explicitly establishes a different tree
```

The signature must have the tree described by the match expression. Parentheses
therefore affect validation and matching, not merely display formatting.

## Locations

The native parser reports byte offsets. The Python AST adds the source path,
start/end offsets, one-based line and column. Columns are computed from source
bytes, so UTF-8 comment text may occupy more than one byte per character.
Native profile locations retain path, line and column for diagnostics and traces.

**Inspect:** `python3 -m blackstone parse examples/kernels/Reassociate.bst`
demonstrates block comments, multiline signatures, grouped imports and explicit
operator grouping in an actual source file.

[Previous](03-first-rewrite.md) · [Contents](README.md) · [Next: Kernels and inheritance](05-kernels-and-inheritance.md)
