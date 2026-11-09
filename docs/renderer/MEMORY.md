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
| Phase | 0, audit and baseline: docs done, baseline numbers pending |
| Completed | Architecture audit, capability matrix, research notes, roadmap, many-instance bench |
| Active task | 0.5 Run the baseline (`instance_bench.ps1`, timing and `-Stages`, Vulkan and D3D12, safe and separate render thread) and fill [benchmarks.md](benchmarks.md) |
| Next | Phase 1.1 slot allocator (only after the baseline confirms where render CPU goes) |
| Last verified build | `bin/` editor at `4bc2a22774` (2026-11-08); no engine code changed on this branch yet |
| Last verified tests | None on this branch yet (docs and bench only) |

## Known problems

- Baseline run 1 failed (script used functions GDScript cannot call) and
  ran without a hard stop; fixed with RD timestamps and `--quit-after`.
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
