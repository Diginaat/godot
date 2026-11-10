---
type: Overview
title: Core modernization phase-zero checkpoint
description: Dedicated branch, source/ABI constraints, negative prototype results, benchmark infrastructure and sequential implementation gates.
tags: [core, architecture, containers, hashing, simd, benchmarks]
status: draft
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-10T00:00:00Z }
sources:
  - id: memory
    resource: ../../docs/core-modernization/memory.md
    title: Resumable project memory
  - id: benchmarks
    resource: ../../docs/core-modernization/benchmarks.md
    title: Initial evidence and missing coverage
  - id: methodology
    url: https://martin.ankerl.com/2019/04/01/hashmap-benchmarks-01-overview/
    title: Hashmaps Benchmarks — Overview (2019), user-supplied reference
---

# Checkpoint

Baseline `172e099b95307708005dafcf719693d20b1e7088`; new branch
`feat/core-stl-simd-modernization` from `dev-stl-core`. Preserve inherited dirty
implementation/configuration/import-fixture work; do not stage it wholesale.
This checkpoint accepts documentation and independently usable benchmark tools,
not the existing prototype engine modifications.[^memory]

Initial evidence rejects the existing numeric hash adapter as a performance
upgrade (+50–57% live bytes and up to ~2.64x operation time). Finite float32
packed Vector3 explicit SSE dot/cross were ~41% slower; conversion-free SoA
throughput is not an API-preserving drop-in gain. Public layouts, shared-value
semantics, persisted/public hashes and C++17 remain constraints.[^benchmarks]

The user-supplied 2019 study motivates map × hasher comparisons, hit-rate/churn/
iteration coverage and separate-process medians. Its Linux/GCC rankings cannot
select a winner for this fork's compiler, adapter contracts or workloads.
unordered_dense remains a maintenance/allocator/layout comparison candidate.
Latest user direction makes an engine-owned hashmap the primary Phase 2 design;
reuse original Godot's native hashing machinery where appropriate, and compare
thirdparty implementations as controls, not mandatory backends.[^methodology]

Next: baseline contract/allocation matrix and a narrow unique-storage experiment
in Phase 1; custom native hashing in Phase 2; strings then validated x86 before
NEON. Full-platform and representative engine-workload acceptance is pending.
