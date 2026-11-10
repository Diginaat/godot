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
| Phase | 2, GPU path for eligible instances: 2.1-2.4, 2.5 index LODs, shadow meshes, motion-pass views, render info and incremental buckets done (2026-11-10); Phase 1 done except 1.8 |
| Completed | Architecture audit, capability matrix, research notes, roadmap, many-instance bench, baseline (Vulkan, D3D12, safe/separate) |
| Active task | None |
| Next | Phase 2.5 rest: `gpu_drawn` in the cull's packed data, occlusion; then Phase 3 multi-draw (roadmap.md) |
| Last verified build | Production editor and the separate tests build (`extra_suffix=tests`), 2026-11-10 |
| Last verified tests | `gpu_instances.ps1` on Vulkan and D3D12 (26 count and render info checks, with index LODs, shadow meshes, a motion pass and per-frame churn without rebuilds; 9 of 9 images and the TAA and FSR 2 motion vectors equal to the CPU path; self-test caught), `instance_table.ps1` on both, Vulkan large-world suite, standard smoke, 2026-11-10 |

## Phase 2 in one line

`rendering/geometry/gpu_instances/enabled` (off by default): `massive_100k`
render CPU 22.0 to 5.5 ms, `shared_20k` about 4 ms lower, images identical
to the CPU path; the rest is mostly CPU shadow passes. Details:
[benchmarks.md](benchmarks.md).

## Baseline in one line

Render-thread CPU bound in every case above 1k nodes: `massive_100k` 20.4 ms
CPU vs 1.15 ms GPU, of which 16.4 ms is main-pass render-list preparation
(fill 5.6, sort 3.2, instance data 7.5); the threaded cull is 1.2 ms. Details: [benchmarks.md](benchmarks.md).

## Known problems

- Open from the baseline: `unique_10k` frame is 3.6-4 ms above render CPU
  (unexplained); D3D12 uses about 750 MiB video memory for 10k materials
  (Vulkan 104 MiB); about 2 ms spread between repeats in the 20k cases.
- With the table on, `dynamic_20k` still has about +0.5-0.8 ms render CPU
  (building/packing 2,000 dirty records per frame); GPU cost fixed in 1.3.
- GPU path: LOD levels a view never uses still cost one empty indirect
  draw each (twice with a shadow mesh); a new bucket key or a full bucket
  still rebuilds the scenario's buckets (7.6 ms at 100k); `unique_10k` gains nothing (one bucket per material); viewport render info
  of GPU-drawn instances is a few frames late (read back from the cull).
- RD cannot draw with a GPU-written count (driver code exists, no RD API,
  Vulkan feature not enabled); D3D12 ignores the indirect stride.

## Decisions

- D025 (in [knowledge/TECHNICAL_DECISIONS.md](../../knowledge/TECHNICAL_DECISIONS.md)):
  extend Forward+ with a persistent GPU instance table first; GPU culling
  and indirect submission of ordinary instances build on it; parallel
  render-list preparation in parallel as the CPU fallback's speed-up.
- D026: the table record (128 bytes) holds only per-instance values that are
  the same in every view and pass; fade, motion and GI bits stay per render
  element, per-surface values (compressed AABB, UV scale) per draw.
- D027: no shader read path or slot-ID elements (1.5-1.7). Measured, the
  CPU per-element walk (about 63 ns per element, pointer chasing) dominates;
  only keeping eligible instances out of the CPU camera lists pays. Phase 2
  writes the existing 176-byte instance data on the GPU per view, so the
  scene shader does not change.
- D028: the GPU path's instance data has its own binding (47) and a scene
  shader specialization bit, because D3D12 upload heaps (the per-frame
  instance buffer) cannot be written by the GPU. Within a render, the cull
  and the buckets agree: joins start at the next cull, and instances that
  leave during a render are drawn by the CPU list that time.
- D029: index LODs on the GPU path: instance data at each member's own
  index plus a visible-ID list per bucket level (binding 48), because D3D12
  has no base instance; the kernel uses the CPU's LOD rule.
- D030: views with a motion pass: moved members get their own commands per
  bucket level, marked per element (`gpu_motion`), drawn by the depth
  pre-pass and the motion pass.
- D031: buckets updated in place (spare slots, free lists, key map); full
  rebuild only for new keys, full buckets, mesh changes, fragmentation.
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
