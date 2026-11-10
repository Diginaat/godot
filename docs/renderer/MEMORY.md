# GPU-first renderer: session memory

Entry point for the Forward+ GPU-driven renderer work. Read this first in
every session, then [roadmap.md](roadmap.md). Keep it short; details go in
the linked files.

## Objective

Make the Forward+ renderer scale to very many instances, large worlds and
many lights by moving per-instance CPU work (render lists, instance data,
culling, submission) to persistent GPU data and GPU passes, without
breaking Godot's node workflow, the fork's features (DDGI, path tracer,
PhysX, CLOD, weather) or the CPU fallback.

## State

| Item | Value |
| --- | --- |
| Branch | `dev-gpu-renderer` (from `dev` at `4bc2a22774`) |
| Phase | 1, GPU instance table: 1.0 done (2026-11-10) |
| Completed | Architecture audit, capability matrix, research notes, roadmap, many-instance bench, baseline (Vulkan, D3D12, safe/separate) |
| Active task | None |
| Next | Phase 1.1 slot allocator (stable slot per `GeometryInstanceForwardClustered`) |
| Last verified build | Production editor with the 1.0 markers, 2026-11-10 |
| Last verified tests | Standard smoke (PhysX game/editor, path tracer, DDGI) and `instance_bench.ps1 -Stages` (Vulkan) pass, 2026-11-10 |

## Baseline in one line

Render-thread CPU bound in every case above 1k nodes: `massive_100k` 20.4 ms
CPU vs 1.15 ms GPU, of which 16.4 ms is main-pass render-list preparation
(fill 5.6, sort 3.2, instance data 7.5); the threaded cull is 1.2 ms. Details: [benchmarks.md](benchmarks.md).

## Known problems

- Open from the baseline: `unique_10k` frame is 3.6-4 ms above render CPU
  (unexplained); D3D12 uses about 750 MiB video memory for 10k materials
  (Vulkan 104 MiB); about 2 ms spread between repeats in the 20k cases.
- RD cannot draw with a GPU-written count (driver code exists, no RD API,
  Vulkan feature not enabled); D3D12 ignores the indirect stride.

## Decisions

- D025 (in [knowledge/TECHNICAL_DECISIONS.md](../../knowledge/TECHNICAL_DECISIONS.md)):
  extend Forward+ with a persistent GPU instance table first; GPU culling
  and indirect submission of ordinary instances build on it; parallel
  render-list preparation in parallel as the CPU fallback's speed-up.
- Earlier geometry decisions D001-D024 still hold (compute + indirect before
  mesh shaders, stable source IDs, opt-in settings, conventional fallback).

## Links

- [README.md](README.md) - index of these docs
- [architecture.md](architecture.md) - frame pipeline, threads, CPU vs GPU, code paths
- [compatibility.md](compatibility.md) - backend capability matrix, fallbacks
- [roadmap.md](roadmap.md) - phases, tasks, acceptance criteria
- [benchmarks.md](benchmarks.md) - results
- [testing.md](testing.md) - gates and bench protocol
- [research.md](research.md) - sources and what they mean here
- [knowledge/PORTING_STATUS.md](../../knowledge/PORTING_STATUS.md) - existing GPU geometry work (MultiMesh culling, CLOD, HZB, streaming)
