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
| 1.0 Split the hotspot (done 2026-11-10: fill 5.6, sort 3.2, instance data 7.5 ms at 100k) | `RENDER_TIMESTAMP` markers (per pass, not per object) between `_fill_render_list`, sort and `_fill_instance_data` in RFC `_render_scene` and `_render_shadow_append` | `-Stages` shows how the 16.4 ms of "Setup 3D Scene" in `massive_100k` splits; update the expected gain of 1.6 (IDs instead of records) against Phase 2 (no per-instance CPU list) |
| 1.1 Slot allocator (done 2026-11-10: `forward_clustered/gpu_instance_slots.h`, `tests/servers/rendering/test_gpu_instance_slots.cpp`) | Stable slot per `GeometryInstanceForwardClustered` (free list, generation). No GPU use yet | Unit test: add/remove/reuse; slots dense after churn; no behavior change, smoke pass |
| 1.2 Table storage + CPU mirror (done 2026-11-10, D026: 128-byte record without per-view and per-surface fields; `forward_clustered/gpu_instance_table.*`; `instance_table.ps1` on Vulkan and D3D12) | `GPUInstanceTable` in `forward_clustered/` (new file): CPU mirror of the record (transform, AABB, flags, layer mask, instance uniforms offset, GI offset, uv scale), dirty bitset, RD storage buffer with growth | Fixture reads the buffer back (test only) and matches the CPU mirror after add/move/remove/grow |
| 1.3 Dirty uploads (done 2026-11-10: up to 16 runs direct, more through one packed upload and `gpu_instance_scatter.glsl`; `dynamic_20k` GPU 4.32 to 1.43 ms) | Measured in 1.2: one `buffer_update` per contiguous range costs +2.9 ms GPU and about +1 ms render CPU for 2,000 scattered moved records (`dynamic_20k`). Replace scattered ranges with one upload of the packed dirty records plus a compute scatter into the table; keep direct range updates for a few long runs | Bench `dynamic_20k`: upload bytes per frame proportional to moved instances; `small_1k` no regression |
| 1.4 Previous transforms (done 2026-11-10: table updated for every scene render after the motion age-out; per-element check against `_fill_instance_data`, incl. teleports and a second viewport; image A/B moves to 1.5, where shaders first read the table) | Keep prev transform in the table; a moved slot keeps "moved last frame" so prev is refreshed one frame after it stops | Motion-vector A/B (TAA) exact against the current path for moving, stopping and teleported instances |
| ~~1.5 Shader read path~~ | Dropped 2026-11-10 (D027): measured, the CPU saving would be at most about 2.5 of 20 ms at 100k | - |
| ~~1.6 Main pass on the table~~ | Dropped (D027), folded into Phase 2 | - |
| ~~1.7 Shadow passes on the table~~ | Dropped (D027), folded into Phase 6 | - |
| 1.8 Lifetime and limits | Freed instance while frames in flight; table growth mid-frame; memory reported in `get_memory_usage` and the bench | Lifetime fixture on Vulkan/D3D12; editor smoke; memory line in bench |

Measured before 1.5 (D027): of the 7.8 ms that `_fill_instance_data`
takes at 100k, the 176-byte record write is about 0.6 ms and the transform
transposes about 1.9 ms; about 5.3 ms is walking the elements (chasing each
render element to its scattered instance object). Writing slot IDs instead
of records cannot remove that walk, so the table pays off when the CPU
stops visiting eligible instances at all (Phase 2). The table stays as
built (1.1-1.4); 1.8 is still needed. Setting
`rendering/geometry/gpu_instance_table/enabled` stays off by default until
Phase 2 shows a gain.

## Phase 2 - GPU path for eligible instances (P0)

Needs P1. Revised 2026-11-10 (D027): the CPU must stop visiting eligible
instances in the camera passes, so culling, the render list and instance
data for them move to the GPU together. Same kernel design as MultiMesh
culling (cull, scan, ordered scatter; D003, stable order).

Eligible (first version): `MeshInstance3D` surfaces that are opaque or
alpha-scissor, no skeleton or blend shapes, no fade or transparency, no
lightmap, lightmap capture or VoxelGI, single precision, Forward+ main
camera views (not mirrors, reflection probes or XR). Everything else keeps
the CPU path.

- 2.1 Draw buckets (done 2026-11-10, `forward_clustered/gpu_instances.cpp`):
  eligible instance surfaces grouped by the render-element key without
  depth layer, LOD and per-view GI bits, plus mirror; one scenario's buckets
  are rebuilt (sorted, full) when a member joins, leaves or changes, not per
  frame.
- 2.2 Keep eligible instances out of the camera render lists (done): the
  scene cull asks the instance (`RenderGeometryInstance::gpu_drawn`, one
  load per visible instance in the threaded cull) and lists it apart; the
  renderer gets that list and the scenario through
  `RendererSceneRender::set_gpu_instance_view()`. Views the GPU path does
  not cover take the list into the CPU render list. Lights, probes, pairing
  and shadow passes are unchanged. Scene cull eligibility: no visibility
  range or parent, not shadows-only, not exempt from culling.
- 2.3 Compute pass per view (done, `gpu_instance_cull.glsl`): frustum (widened
  for jitter) and camera layers from the table record (now 160 bytes with
  world bounds and two per-instance bits, D028); writes the existing
  176-byte `InstanceData` and appends to the bucket's indirect command.
  The scene shader reads it from binding 47 under a specialization bit
  (D028), so it changes in one macro, not in its logic.
- 2.4 One indirect draw per bucket in the depth pre-pass and the opaque pass
  (done). Views with a motion list (temporal upscaling), reflection probes,
  mirrors, XR and debug draw modes use the CPU list. Multi-draw is Phase 3.
- 2.5 Index LODs done 2026-11-10 (D029): the kernel chooses each instance
  surface's index LOD with the CPU rule and appends it to that level's
  visible-ID list (binding 48); one indirect draw per bucket level. Meshes
  with LODs are eligible; `gpu_instances.ps1` has an index-LOD phase (all
  levels drawn, per-level counts equal to the CPU choice, image equal).
  Separate shadow meshes done 2026-11-10: the depth pre-pass draws them
  through a twin command per bucket level with the shadow mesh's index
  counts (the kernel counts into both); a shadow mesh with fewer index LODs
  than the mesh stays on the CPU path. Views with a motion pass (FSR 2,
  DLSS, MetalFX) done 2026-11-10 (D030): moved members get their own
  commands, drawn by the depth pre-pass and the motion pass; motion vectors
  equal to the CPU path under TAA and FSR 2 on Vulkan and D3D12. Viewport
  render info done 2026-11-10: the cull sums objects and primitives per
  workgroup into a per-view counter, read back without stalling and added
  to the view's render info a few frames later; equal to the CPU lists'
  counts in every validation check. Incremental bucket updates done
  2026-11-10 (D031): spare slots, free lists and a key map; a full rebuild
  only for new keys, full buckets, mesh changes and fragmentation. Next:
  the `gpu_drawn` bit in the cull's packed data;
  occlusion culling (`OccluderInstance3D` today, HZB
  later) for them.
- Acceptance: exact pixel and motion-vector A/B against the CPU path on
  Vulkan and D3D12; `massive_100k` and `shared_20k` render CPU down by most
  of the "Setup 3D Scene" share; GPU cost reported per view; `small_1k` not
  slower; no readback.
  Status 2026-11-10 (2.1-2.4): pixel A/B exact in all 7 phases of
  `gpu_instances.ps1` on Vulkan and D3D12 (no motion-vector A/B yet: views
  with a motion list stay on the CPU); `massive_100k` render CPU 22.0 to
  5.5 ms, `shared_20k` about 4 ms lower; GPU +0.1-0.3 ms at 100k; readback
  only for validation. See benchmarks.md.

## Phase 3 - indirect submission (P0)

Needs P2 (which already draws its buckets indirectly).

- 3.1 Multi-draw per pipeline group where `SUPPORTS_MULTI_DRAW_INDIRECT`
  (Phase 2 starts with one indirect draw per bucket).
- 3.2 RD: `draw_list_draw_indirect_count` (Vulkan `drawIndirectCount`
  feature, D3D12 count buffer, Metal fallback loop); fix D3D12 ignoring
  `p_stride`. Upstreamable as its own PR.
- 3.3 Depth, opaque, motion passes; editor viewports; render layers.
- Acceptance: `DRAW_CALLS_IN_FRAME` API calls fall; render CPU down at 20k
  and 100k; `small_1k` no regression; image exact.

## Phase 4 - parallel render preparation (P1)

Independent of P2/P3; helps the CPU fallback path. D027 shows the CPU
element walk is dominated by pointer chasing (about 63 ns per element at
100k): keeping the per-element hot fields (flags, slot, mirror, mesh
instance validity) in the surface cache or the render list itself, so
`_fill_instance_data` does not touch the instance object, is a cheap
candidate before threading.

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
