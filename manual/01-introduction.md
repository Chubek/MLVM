# 1. Introduction and supported scope

[Contents](README.md) · [Next: Architecture](02-architecture.md)

MLVM is a multi-level, multi-dialect compiler infrastructure. It supplies reusable
representations and transformations, a small native toolchain, and an optional C
frontend named Cephyr. Applications can embed the libraries or use command-line
tools to inspect and transform artifacts.

## What is implemented

The 0.1.0 baseline includes contexts and structured diagnostics, a value-owned
generic SSA representation, versioned text, operation registration, verification,
transactional module passes, and full dialect conversion. A narrow x86-64 toolchain
assembles, archives, links, and inspects ELF relocatable objects. Optional
library-backed binary tools broaden object handling. Lua schedulers orchestrate
native Blackstone kernels and Firestone IRs as verified acyclic pipelines.

Cephyr uses cparser-iso11 for a documented C11 freestanding integer subset:
functions, parameters, locals, calls, structured control flow, static assertions
and generic selection. Full ISO C11 conformance remains a development goal.

## Three useful starting points

| Goal | Starting artifact | Primary interface |
| --- | --- | --- |
| Transform SSA | `.mlvm` text or `mlvm::Module` | `mlvm`, `MLVM::IR` |
| Author optimizations | `.bst` kernels and `.sched` orchestrators | `mlvm-sched`, `MLVM::Scheduler` |
| Build native artifacts | supported assembly or Cephyr C | `mlvm-as`, `cephyr`, `mlvm-ld` |

Registered high-, mid-, low-, machine-, interpreter-, GPU-, LLVM- and
WebAssembly-oriented dialects have explicit verification contracts. Standard
integer expression routes connect them to the target emitters. Structured control,
device launches and runtime calls require their own host lowerings.

## Reading support claims

An accepted object-machine identifier does not promise instruction selection for
that machine. A parsed C construct does not promise semantic lowering. Likewise,
a validated Blackstone declaration does not promise that a selected backend can
execute its transformation. Treat each boundary as an explicit contract.

Reference: [implementation status](../docs/ROADMAP.md).
