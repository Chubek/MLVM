# Chapter 6 — Concerns and typed HIR views

A `concerns` declaration registers the HIR view needed by a pattern or result
sort. The reference expression views have this explicit mapping:

| Qualified view | Language sort | Meaning |
| --- | --- | --- |
| `HIR::Literals::Term` | `Term` | A resolved typed value, including expressions |
| `HIR::Expressions::Binary` | `Binary` | A binary expression with an opcode and two operands |

The word `Literals` in the first qualified name does not restrict every Term to
a literal. This is an adapter contract, not a deduction from a host class
hierarchy. Literal restrictions come from `!literal` or `!exact(n)` refinements.

## Declaring both views

```bst
Kernel ExampleViews;
concerns HIR::Literals::Term;
concerns HIR::Expressions::Binary;
```

A rule with a Binary signature and Term leaves needs both views. A `-> Term`
replacement may be an input, a literal or a binary value while retaining the
matched root's exact arithmetic type. A `-> Binary` replacement needs the Binary
view as well as an explicit binary construction.

An unknown qualified view is a declaration error. A known sort without its
required concerns declaration is also an error, even when the host happens to
have nodes that resemble that sort.

## Opaque Term captures

In a signature `Binary(Term!resolves, Term!exact(0))`, the first Term can bind an
entire expression. Its internal operators are not exposed by that signature.
The signature asks to inspect the outer binary and its two operands, not to
recursively reveal every node under the first operand.

The driver still normalizes operand subtrees before matching the parent.
Normalization order and exposed-pattern structure are separate concepts:
the former schedules work; the latter determines what a rule's guards certify.

## Structured carriers

The catalog uses the same two language sorts for host descriptors:

| Term view | Binary view | Carrier domain |
| --- | --- | --- |
| `HIR::Statements::Statement` | `HIR::Statements::Composition` | `statement` |
| `HIR::Memory::State` | `HIR::Memory::Operation` | `memory` |
| `HIR::ControlFlow::Region` | `HIR::ControlFlow::Operation` | `control` |
| `HIR::Loops::Descriptor` | `HIR::Loops::Operation` | `loop` |
| `HIR::Polyhedra::Descriptor` | `HIR::Polyhedra::Operation` | `polyhedral` |

A loop Term can describe bounds or a body plan. These carriers are typed opaque
host objects, not integer expressions merely because they reuse Binary-shaped
syntax. Their transformation proofs and embedded payload semantics belong to the
host, as chapter 22 explains.

The Python registry includes the catalog views. A fresh direct C registry
includes the acceptance expression views and loop descriptor views; additional
modules are registered explicitly. The Python-to-native bridge registers its
frontend catalog contracts before compiling a profile.

**Inspect:** `examples/loop-descriptor.json` uses domain `loop`. Applying the
zero-trip kernel leaves it unchanged because knowing the carrier shape does not
prove that the loop has zero iterations.

[Previous](05-kernels-and-inheritance.md) · [Contents](README.md) · [Next: Capabilities](07-capabilities-and-symbol-resolution.md)
