# GPU-first renderer roadmap

Dependency-ordered milestones for the Forward+ renderer. Each milestone is
one or a few commits on `dev-gpu-renderer`, with its own acceptance
criteria, measured with [testing.md](testing.md). Status lives in
[MEMORY.md](MEMORY.md); decisions in
[knowledge/TECHNICAL_DECISIONS.md](../../knowledge/TECHNICAL_DECISIONS.md)
(D025 onward).

Priorities come from the audit ([architecture.md](architecture.md)) and the
baseline ([benchmarks.md](benchmarks.md)), not from the original wishlist.
Changes against the wishlist are listed at the end.

## Dependency graph

```text
P0 audit + baseline
 └─ P1 GPU instance table (persistent per-instance data, dirty uploads)
     ├─ P2 GPU culling of ordinary MeshInstance3D over the table
     │   └─ P3 cached draw buckets + indirect submission
     │       ├─ P5 HZB / GPU LOD for table instances (reuses mesh_hiz)
     │       └─ P6 GPU shadow caster lists (per-light frusta)
     └─ P4 parallel render-list preparation (independent of P2/P3; CPU path)
P7 lighting/GI scheduling        (independent; measure first)
P8 voxel generation/meshing      (independent module work)
P9 GPU BVH                       (only with a consumer)
P10 viewport/2D cleanup          (independent, small)
P11 stabilization                (after each group)
```

P4 can start before P2: it speeds up the CPU path that stays the fallback
for everything the GPU path does not cover.

## Phase 0 - audit and baseline (this session)

- [x] 0.1 Architecture report: [architecture.md](architecture.md).
- [x] 0.2 Capability matrix: [compatibility.md](compatibility.md).
- [x] 0.3 Research notes: [research.md](research.md).
- [x] 0.4 Many-instance bench: `misc/large_world_test_project/instance_bench.*`,
  `devtools/test/instance_bench.ps1`.
- [x] 0.5 Baseline: [benchmarks.md](benchmarks.md) (Vulkan, D3D12, both
  render thread modes; per-stage CPU on Vulkan).
- [x] 0.6 Branch `dev-gpu-renderer`, docs, MEMORY.md.

## Phase 1 - GPU instance table (P0)

Problem: main-pass render-list preparation takes 16.4 of 20.4 ms render
CPU at 84k visible objects in a static scene (baseline,
[benchmarks.md](benchmarks.md)), about 195 ns per visible object per frame;
shadow lists another 2-3 ms. `_fill_instance_data` rewrites 176 bytes per
render element per pass per frame, even when nothing moved. The GPU has no persistent view of the scene, so no
GPU culling or GPU submission for ordinary instances is possible.

Goal: one persistent GPU buffer per renderer holding every eligible geometry
instance's data, indexed by a stable slot, updated only where it changed.
A parallel representation: `RendererSceneCull` and
`GeometryInstanceForwardClustered` stay the source of truth.

| Task | Change | Acceptance |
| --- | --- | --- |
| 1.0 Split the hotspot | `RENDER_TIMESTAMP` markers (per pass, not per object) between `_fill_render_list`, sort and `_fill_instance_data` in RFC `_render_scene` and `_render_shadow_append` | `-Stages` shows how the 16.4 ms of "Setup 3D Scene" in `massive_100k` splits; update the expected gain of 1.6 (IDs instead of records) against Phase 2 (no per-instance CPU list) |
| 1.1 Slot allocator | Stable slot per `GeometryInstanceForwardClustered` (free list, generation). No GPU use yet | Unit test: add/remove/reuse; slots dense after churn; no behavior change, smoke pass |
| 1.2 Table storage + CPU mirror | `GPUInstanceTable` in `forward_clustered/` (new file): CPU mirror of the record (transform, AABB, flags, layer mask, instance uniforms offset, GI offset, uv scale), dirty bitset, RD storage buffer with growth | Fixture reads the buffer back (test only) and matches the CPU mirror after add/move/remove/grow |
| 1.3 Dirty uploads | Coalesce dirty slots into ranges; `buffer_update` per range, or one compute scatter from a staging ring when ranges are fragmented (decide by measurement, D025) | Bench `dynamic_20k`: upload bytes per frame proportional to moved instances; `small_1k` no regression |
| 1.4 Previous transforms | Keep prev transform in the table; a moved slot keeps "moved last frame" so prev is refreshed one frame after it stops | Motion-vector A/B (TAA) exact against the current path for moving, stopping and teleported instances |
| 1.5 Shader read path | Specialization bit: scene shader reads `table[remap[base + INSTANCE_INDEX]]` (same remap idea as MultiMesh culling, binding 46) | Pixel A/B exact on Vulkan and D3D12: static, moving, instance uniforms, lightmaps, VoxelGI, layer masks |
| 1.6 Main pass on the table | Eligible opaque elements write 4-byte slot IDs instead of 176-byte records | `shared_20k`, `massive_100k`: lower render CPU and upload bytes; GPU p50 within noise |
| 1.7 Shadow passes on the table | Shadow lists write slot IDs too | Shadow A/B exact (directional 4 splits, spot, omni cube); bench gain reported |
| 1.8 Lifetime and limits | Freed instance while frames in flight; table growth mid-frame; memory reported in `get_memory_usage` and the bench | Lifetime fixture on Vulkan/D3D12; editor smoke; memory line in bench |

Eligibility (first version): opaque or alpha-scissor `MeshInstance3D`
without skinning/blend shapes, single precision. Everything else keeps the
176-byte path. Setting `rendering/geometry/gpu_instance_table/enabled`,
default off until 1.7 shows a gain.

## Phase 2 - GPU culling of ordinary instances (P0)

Needs P1. Same kernel design as MultiMesh culling (cull, scan, ordered
scatter; D003, stable order).

- 2.1 Static draw buckets: group eligible instances by the render-element
  key without depth layer and LOD (pipeline, material, mesh surface); rebuilt
  on change, not per frame.
- 2.2 Compute frustum cull + layer mask + visibility range over each bucket's
  slots into compacted ID lists; CPU cull still runs for lights, probes, GI
  and notifiers, but eligible instances skip `_fill_render_list`.
- 2.3 Per-instance LOD in the kernel (reuse D010).
- 2.4 Multiple views (each viewport its own lists), jitter (widened frustum),
  first frame and camera cuts (current-frame, no history).
- Acceptance: exact pixel A/B against CPU culling; `massive_100k` render
  CPU down; GPU cost reported per view; no readback.

## Phase 3 - indirect submission (P0)

Needs P2.

- 3.1 One indirect record per bucket per LOD; multi-draw per pipeline group
  where `SUPPORTS_MULTI_DRAW_INDIRECT`.
- 3.2 RD: `draw_list_draw_indirect_count` (Vulkan `drawIndirectCount`
  feature, D3D12 count buffer, Metal fallback loop); fix D3D12 ignoring
  `p_stride`. Upstreamable as its own PR.
- 3.3 Depth, opaque, motion passes; editor viewports; render layers.
- Acceptance: `DRAW_CALLS_IN_FRAME` API calls fall; render CPU down at 20k
  and 100k; `small_1k` no regression; image exact.

## Phase 4 - parallel render preparation (P1)

Independent of P2/P3; helps the CPU fallback path.

- 4.1 Measure `_fill_render_list` + sort + `_fill_instance_data` per pass
  with Tracy zones (add `GodotProfileZone` in RFC).
- 4.2 Build shadow pass lists in parallel (WorkerThreadPool, one task per
  pass, thread-local lists, ordered merge). Deterministic output.
- 4.3 Split the main pass fill across workers by element range.
- 4.4 Evaluate RDG secondary command buffers
  (`SECONDARY_COMMAND_BUFFERS_PER_FRAME`) on NVIDIA/AMD/Intel; keep off where
  drivers misbehave.
- 4.5 Remove per-frame heap allocations in the cull path (planes, light
  vectors, convex points).
- Acceptance: render CPU down with `lights_20k` and 4 cascades; identical
  render lists (debug hash) single vs multi-threaded.

## Phase 5 - HZB and GPU LOD for table instances (P1)

Reuse `mesh_hiz.cpp` (current-frame pyramid after the pre-pass; D023/D024).
Add table instances to the colour-pass occlusion test; evaluate two-pass for
table instances only after compacting phase-2 commands (the city bench shows
the fixed phase-2 cost). Acceptance: city bench and `massive_100k` with
occluders; exact images.

## Phase 6 - GPU shadow caster lists (P1)

Per shadow view (cascade, spot, omni face) cull table instances against the
light frustum, never the camera's. Keep the far-cascade cache. Acceptance:
casters outside the camera frustum still cast; shadow A/B exact; shadow-pass
render CPU down.

## Phase 7 - lighting and GI scheduling (P2)

Light binning is already on the GPU. First measure `update_light_buffers`
and `bake_cluster` with 128 / 512 / 2048 lights; warn or raise the 512
clustered-element cap instead of dropping lights silently. DDGI: rebuild the
TLAS only when instances changed (refit/update otherwise). No async compute
(no queue in RD).

## Phase 8 - GPU voxel generation and meshing (P2)

Separate module work on top of `VoxelField3D`. Collision stays on the CPU and
PhysX-independent. Start only with a target project.

## Phase 9 - GPU BVH (P2)

Only with a consumer. Candidate: DDGI/path tracer TLAS updates (driver
acceleration structures already exist). No shared engine BVH.

## Phase 10 - viewport cleanup (P3)

Measure the empty-canvas cost of a 3D-only window; skip canvas work when the
viewport has no canvas items. Small, upstreamable.

## Phase 11 - stabilization

After each group: full smoke, large-world fixtures, valley and city benches,
editor responsiveness, PhysX/DDGI/path tracer projects, remove dead code.

## Changes against the wishlist

- **GPU frustum/occlusion culling and HZB already exist** for MultiMesh and
  CLOD. The remaining gap is ordinary instances, which first need P1.
- **Main CPU cull is already parallel** (linear scan over packed arrays on
  all workers). The serial cost is render-list building and recording,
  repeated per shadow pass; P1 and P4 target that.
- **Clustered light binning is already GPU-side.** P7 starts with
  measurement only.
- **Async GI scheduling is not possible today**: RD has one queue for
  graphics and compute. Adding an async compute queue to RD is a large,
  separate project; not planned until a measurement shows idle GPU time.
- **Mesh shaders** are not exposed by RD; CLOD covers the virtual geometry
  need with compute plus indirect draws (D002).
