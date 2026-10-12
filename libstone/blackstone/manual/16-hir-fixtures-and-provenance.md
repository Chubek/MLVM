# Chapter 16 — HIR fixtures and provenance

The reference CLI accepts a JSON object with `root` and `nodes`. `root` names a
node ID; `nodes` maps IDs to node descriptions. References can share values and
form an acyclic graph, while operand occurrences retain tree evaluation semantics.

## A complete fixture

```json
{
  "root": "sum",
  "nodes": {
    "x": {"kind": "input", "type": {"width": 8}, "pure": true, "total": true},
    "zero": {"kind": "literal", "type": {"width": 8}, "value": "0"},
    "sum": {"kind": "binary", "type": {"width": 8}, "op": "plus", "args": ["x", "zero"], "provenance": "sample:1"}
  }
}
```

Each node has its own type metadata. A missing type defaults to domain `integer`,
width 32 and unsigned interpretation. Specifying width on one node does not
change other nodes' defaults.

## Node fields

| Field | Meaning/default |
| --- | --- |
| `kind` | Required: `input`, `literal` or `binary` |
| `type.domain` | `integer` by default; other strings describe host carriers |
| `type.width` | Positive integer; default 32 |
| `type.signed` | Boolean; default false |
| `resolved` | Boolean; default true |
| `provenance` | Source/debug label; default the input node ID |
| `origin` | Originating rewrite rule; default empty |

Input nodes use `op` as a descriptive name, defaulting to the node ID. They may
specify `pure` and `total` as true, false or null; missing facts are unknown.
Literal nodes have a representable decimal `value` and are intrinsically pure and
total. Use strings for large numeric values to avoid precision loss in other JSON
consumers.

Binary nodes require an `op` and exactly two operand IDs in `args`. `flags` is a
string list, empty by default. Binary `pure`/`total` values can be explicit host
proofs; otherwise the adapter derives conservative facts. Writing a true fact
asserts a proof supplied by the fixture author, rather than triggering an analysis.

## Identity and graph validation

IDs represent identity. `args: ["x", "x"]` shares one input; separate IDs named
`left` and `right` remain distinct even if their metadata matches.
Unknown references, invalid fields and cycles are diagnostics. The loader checks
unreachable nodes too, so an unreachable cycle does not enter the adapter.

The loader validates serialization structure and literal representability.
Mixed-type binaries can still be represented to demonstrate guard nonmatches;
successful loading does not certify every node as an eligible modular operation.
Likewise, descriptor names alone do not prove host transformations.

## Output and debug metadata

`dump_fixture` assigns deterministic output IDs, preserves sharing and includes
provenance, origin, resolvedness and analysis facts. It does not preserve original
ID spelling. Compare semantic structure and identity relationships rather than
expecting an output ID named `sum`.

New nodes carry the matched root's provenance and the creating rule's origin.
Reused captures keep their existing provenance/origin. The reassociation fixture's
result has root origin `ReassociateAddConstants` and right-literal origin
`ConstantFolding`, both with `demo:1` provenance.

**Try:** run `apply` with `examples/reassociate.json --trace`, and inspect the
optimized JSON separately from stderr's rule events.

[Previous](15-replacement-construction.md) · [Contents](README.md) · [Next: Scheduling](17-scheduling-and-convergence.md)
