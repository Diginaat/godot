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
| Phase | GPU path on game levels (2026-11-15, D038: commands only where a member can land, small shadow passes on the CPU list, one shadow cull per render; D039: commands cleared on the GPU, no per-view uploads); material parameter tables done (D036); 5.1 HZB occlusion done (D034); 6.1-6.3 shadow casters on the GPU path done; Phases 1 and 2 done |
| Completed | Architecture audit, capability matrix, research notes, roadmap, many-instance bench, baseline (Vulkan, D3D12, safe/separate) |
| Active task | GPU-first plan (roadmap.md, 2026-11-24): G1 done (HZB occlusion for CPU render list meshes, `rendering/geometry/mesh_occlusion/enabled`); G2 done (adaptive VRS, main-viewport default); G3 MultiMesh part done (two-pass MultiMesh occlusion, batched selection dispatches, valley neutral); G5 done (CLOD-eligible surfaces not batched, CLOD budget 2^20 groups); G3 done on all paths (CPU list `mesh_occlusion/two_pass`, GPU path `gpu_instances/occlusion_two_pass`); CLOD compact command streams with indirect-count draws done 2026-11-27 (kb compact-clod-commands; `hzb_bench` GPU frame 2.2-5.5x faster on Vulkan); CLOD in shadow passes done 2026-11-27 (kb shadow-clod; shadow stage 9-11x faster, GPU frame 2.2-2.6x in `shadow_clod_bench`); 2026-11-27 also: `cluster_lod/enabled` off by default (D041; the large-world test project turns it on), lights over the cluster limit keep the nearest (kb light-cap-nearest), cluster render buffer laid out for the used elements (kb cluster-render-buffer-used-size, `light_bench`), TLAS kept in static frames (kb tlas-static-skip), valley smoke fixed for G1; G6 half-size SSAO/SSIL/SSR already default upstream; positional shadows already redraw only when casters move (upstream `make_shadow_dirty`); an adaptive depth pre-pass needs a second opaque colour pipeline (EQUAL without write vs GREATER_OR_EQUAL with write), not started; next: G4 receiver-aware shadow caster culling from the camera depth (GPU path and MultiMesh shadow culls run after the pre-pass, in `_render_shadow_process()`); indirect-count draws for the GPU path gain little (one command per bucket element, read-back counts gained at most 0.03 ms) |
| Next | The GPU path's draws on game levels: about 50-60 ns more GPU time per draw than the CPU list's in depth-only passes (depth pre-pass +0.02-0.03 ms, shadow draws +0.03-0.05 ms with every pass on it; benchmarks.md "A game level: where the GPU path's cost is"); not the indirect draw (direct draws with read-back counts gained at most 0.03 ms); next suspects the visible-ID load in the vertex shader, or a GPU profiler capture of a cube face; the culls themselves are cheap (camera below 0.01 ms, a shadow batch about 7 us); the scene cull of GPU path members (up to 1.1 ms at 100k, benchmarks.md "Where render CPU goes after D036"): experiment D037 on branch `dev-gpu-cull` (GPU-culled per-frame cascades work, gain within spread; cached far cascades gave an unexplained A/B difference; read D037 there before continuing); moved instances: render side down 9% (benchmarks.md "Moved instances"), next is the main thread's share (node propagation, notifications, physics sync); bindless textures; multi-draw has little left to merge (2-11 buckets in the bench) |
| Last verified build | Production editor, 2026-11-27 (branch `dev-renderer-next`, pushed to the fork) |
| Last verified tests | `gpu_instances.ps1` on Vulkan and D3D12 (42 camera and 42 shadow count checks, render info, index LODs, shadow meshes, a motion pass, an occluder, HZB occlusion behind a wall, spot and omni shadows, casters behind the camera and per-frame churn without rebuilds; 13 of 13 images, shadow render info over 12 frames and the TAA and FSR 2 motion vectors equal to the CPU path; self-test caught), large-world suites on Vulkan and D3D12, standard smoke, 2026-11-10; after D036 (2026-11-15): `gpu_instances.ps1` Vulkan and D3D12, large-world Vulkan, standard smoke, instance bench Vulkan and D3D12; after D038 (2026-11-15): `gpu_instances.ps1` Vulkan and D3D12 (15 of 15 images, a 150-mesh LOD phase with a motion pass, shadow passes on the CPU list; a forced level-0 mask fails it), standard smoke, large-world Vulkan, instance bench Vulkan (both paths); after the batched shadow cull: `gpu_instances.ps1` Vulkan and D3D12, instance bench Vulkan, standard smoke; after D039: `gpu_instances.ps1` Vulkan and D3D12 (with the index count check), standard smoke, large-world Vulkan, instance bench Vulkan |

## Material tables in one line

D036: one storage-buffer slot per material, uniform sets shared per page
and texture set, group id in the sort key: `unique_10k` render CPU 7.5-8.1
to 0.42-0.47 ms, frame 13 to 0.85 ms, draws 8,776 to 11. Details:
[benchmarks.md](benchmarks.md).

## Shadows in one line

D033 and the motion list: `massive_100k` render CPU 1.5-1.6 ms (CPU path
19.3-19.7 ms; frame 1.9-2.0 ms, GPU 1.3 ms), `shared_20k` 0.57 ms. Details:
[benchmarks.md](benchmarks.md).

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
- GPU path: in views with many instances per bucket command (D038 does
  not walk them), LOD levels the view never uses still cost one empty
  indirect draw each (twice with a shadow mesh); a new bucket key or a full bucket
  still rebuilds the scenario's buckets (7.6 ms at 100k); `unique_10k` gains nothing (one bucket per material); viewport render info
  of GPU-drawn instances is a few frames late (read back from the cull).
- GPU path on a game level with many meshes and few copies (public
  Overgrown Subway demo): 0.27-0.30 ms slower than the CPU path, cut to
  +0.04-0.08 ms (A/A floor 0.06) by D038 (commands only where a member
  can land, small shadow passes on the CPU list, one cull dispatch for all
  shadow passes of a render). The cause was the shadow passes' fixed cost,
  not the empty draws. D039 removed the command uploads with no measurable
  GPU change: what is left is in the draws (about 50-60 ns more per draw in
  the depth pre-pass and shadow passes), not in the culls, so small passes
  still lose and the 256 cutoff stays (benchmarks.md, "A game level").
- GPU path memory: +69 MiB of buffers at 100k nodes (125.5 vs 56.9 MiB):
  table 21 MB, instance data 23 MB at power-of-two capacity (shared by the
  camera and shadow passes since 2026-11-10; it was +90 MiB with a second
  copy), visible IDs and commands per pass.
- `draw_list_draw_indirect_count()` exists since 2026-11-27 (CLOD only so
  far); D3D12 ignores the indirect stride.

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
- D032: the kernel draws members whose slot bit the scene cull set (exact
  frustum, layers, occluders); the cull lists the slots (+0.2 ms at 100k).
- D033: directional cascades on the GPU path (per-pass kernel run, shared
  shadow instance data); used-bucket filter when buckets are many.
- D034: HZB occlusion of GPU path members for the colour passes (second
  kernel run after the pre-pass, second commands and IDs).
- D035: the GPU path, its occlusion, the table, MultiMesh GPU culling
  (shadows, occlusion), cluster LOD (occlusion, streaming) are on by
  default; fixtures that test one path pin the others.
- D036 material parameter tables, D038 GPU path on game levels (see
  above).
- D039: views clear their indirect commands on the GPU (no uploads); the
  cull writes each filled command's index count; bucket data uploaded
  when it changes.
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
