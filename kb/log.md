# Update log

## 2026-11-27 (previous transforms out of the instance table, O1)

* **Added**: [previous transform table](/project/prev-transform-table.md):
  the instance table record drops its previous transform (160 to 112
  bytes); instances that moved last frame have an entry in a separate
  table (OD12). city_extreme 232 to 222 MB, churn_100k 131.5 to 120.3 MB,
  GPU time unchanged. A first version without compaction uploaded 20,000
  entries a frame for 2,000 movers (+5% GPU in dynamic_20k after another
  case); recorded there.
* **Updated**: [instance data built in the shader](/project/gpu-instance-data-in-shader.md):
  "Still open" points to OD12.

## 2026-11-27 (global transforms in a loop, O2)

* **Added**: [global transform loop](/project/global-transform-loop.md):
  `Node3D::get_global_transform()` updates the dirty ancestors in a loop
  instead of recursing (OD11). scene_bench deep 100k 18.7 to 15.1 ms
  (stock 18.2), deep 10k 0.465 to 0.432 ms. A prefetch in the dirty walk
  was tried and dropped (wide and parents 34-43% slower).

## 2026-11-27 (GPU instance data built in the scene shader, O1)

* **Added**: [instance data built in the shader](/project/gpu-instance-data-in-shader.md):
  no per-member `InstanceData` copy; the scene shader reads the table
  record, member, bucket and view flags (OD10). city_extreme 287 to 232 MB,
  forest 165 to 149 MB, massive_100k GPU 0.948 to 0.903 ms.
* **Updated**: [GPU buffer VRAM](/project/gpu-buffer-vram.md): the
  per-member instance data is gone.

## 2026-11-27 (VRAM after an unload, O1)

* **Added**: [VRAM after an unload](/project/vram-shrink-after-unload.md):
  lowest-first GPU instance slots, `gpu_buffer_fits()` shrink rule, CPU
  render list instance buffers, the RenderingDevice upload staging ring
  and the transfer worker staging buffers shrink. small_1k after
  massive_100k 300 to 83 MB (fresh 70), level bench city_extreme 415 to
  287 MB, forest 221 to 165 MB.

## 2026-11-27 (native property access, O5)

* **Added**: [native property access](/project/native-property-validated-call.md):
  `Object::get_native()` and `set_native()` call getters and exactly typed
  setters with `validated_call()`; objects, arrays, dictionaries and
  converted values keep `call()`. scene_bench script 10k 1.132 to 1.075 ms,
  100k 23.6 to 22.1 ms.

## 2026-11-27 (HZB build, O3)

* **Added**: [HZB build bound by barriers](/project/hzb-fused-reduce.md):
  small_1k GPU time per pass against stock (gap 0.039 ms, HZB build 0.018
  of it); the reduction now writes four levels per dispatch. HZB 0.018 to
  0.010 ms (small_1k), 0.033 to 0.021 ms (forest_map). `hzb.gd` checks a
  second, multi-group pyramid. `instance_bench.gd --case=NAME` and
  `gpu_intervals.gd --prof-min=MS` for profiling one case.

## 2026-11-27 (ubershader cold compile, O4)

* **Added**: [fork code in the ubershader](/project/ubershader-cold-compile.md):
  the ubershader loads the drawn instance once per stage instead of
  choosing a buffer at each of 57 field reads, and the DDGI probe capture
  bit is a pipeline constant of capture ubershaders (precompiled once a
  view uses DDGI). Cold shader bench: ubershader driver CPU 545 to 496 s,
  stall 48.4 to 44.3 s; images the same, checked with every draw forced
  through the ubershader.

## 2026-11-27 (process loop, O2/O5)

* **Added**: [scripted node processing](/project/process-loop-misses.md):
  `GDScriptInstance::notification()` no longer allocates when no script in
  the chain has `_notification()`, and the process loop prefetches the
  script instance and a Node3D's local transform. scene_bench script
  10k 1.486 to 1.141 ms, 100k 36.1 to 23.6; native unchanged (10k still
  0.02 ms behind stock). Two prefetch variants that slowed native
  processing are recorded.

## 2026-11-27 (GPU buffer VRAM, O1)

* **Added**: [VRAM of the GPU-driven systems cut](/project/gpu-buffer-vram.md):
  growing GPU buffers get an eighth of headroom above 65,536 elements
  instead of the next power of two, and the HZB starts at half
  resolution. city_extreme 475 to 415 MB, forest 237 to 221, town 201
  to 192, a 1k-node scene 80.2 to 72.2 (stock 68.8); GPU time slightly
  lower. Start of the engine performance brief in `docs/optimization/`.

## 2026-11-27 (mesh occlusion bench, G9)

* **Updated**: [mesh occlusion](/project/mesh-occlusion.md): new
  `mesh_occlusion_bench.tscn` (SSS spheres in walled courtyards). It found
  CPU-list batches whose one merged box never hid. Batches now stay local
  (8x a member's size) while mesh occlusion is on. Street GPU frame 1.043
  to 0.570 ms; open view +0.017 ms GPU, +0.055 ms CPU; same images.

## 2026-11-27 (light fill CPU, G7)

* **Added**: [render-thread cost of many lights halved](/project/light-fill-cpu.md):
  `Light::linear_color` (no `pow()` per light per frame), a radix sort
  of the light depth order, and instanced draws in `bake_cluster()`.
  `cluster_render.glsl` now merges helper lanes by cluster and element;
  merging by cluster only lost element bits once a draw held many
  elements. 4096 lights: render CPU 0.888 to 0.470 ms, GPU 1.697 to
  1.675 ms, images bit-identical (`light_bench.gd --image-dir`).

## 2026-11-27 (shadow receiver culling, G4)

* **Added**: [receiver culling of directional shadow casters](/project/shadow-receiver-culling.md):
  per-cascade light-space receiver masks from the final depth pre-pass
  (4 x 4 pixel blocks) and visible transparent bounds; the GPU-path shadow
  cull skips casters with no receiver behind them. Adaptive (masks only
  while they cull at least 2^20 primitives). Same image in the new
  `shadow_receivers` test. City bench with every cascade rendered per
  frame: GPU frame 0.764 to 0.609 ms (street), 0.830 to 0.647 (rooftop);
  neutral with the default far-cascade cache and in the valley.
  Failed first versions: 16-pixel tiles from the slice start (nearly
  nothing culled), a culled-fraction rule (lost 0.02 ms on few cheap casters).

## 2026-11-27 (VRS on D3D12)

* **Fixed**: [attachment VRS on D3D12](/project/d3d12-vrs-attachment.md):
  the rate image was filed as a colour attachment and never bound, and the
  driver's maximum texel size (8) made RD build 8 pixel tiles for a 16
  pixel GPU tile. Both also in upstream. Adaptive VRS on D3D12 went from
  27-39 dB (fails) to 47.5-47.9 dB at threshold 0.1, like Vulkan. The
  adaptive kernel reduces with subgroup sums (two barriers instead of
  eight) and has direct "VRS Adaptive" timestamps.

## 2026-11-27 (VRS off by default)

* **Changed**: `rendering/vrs/mode` defaults to Disabled (D042, the
  maintainer's request). Measured: Adaptive costs 0.205 ms on D3D12 with
  no opaque saving, and breaks even on Vulkan, in `light_bench`
  ([adaptive VRS](/project/adaptive-vrs.md)).

## 2026-11-27 (skinned crowd)

* **Added**: [skinned crowd costs](/project/skinned-crowd-costs.md) and
  `devtools/test/skin_bench.ps1`: GPU skinning already runs only for
  culled-in instances; setup CPU is about 0.5 ms per 1000 characters,
  whatever is in view. Bone AABBs are precomputed in skeleton space
  (identical results; within noise end to end).

## 2026-11-27 (cluster LOD off by default)

* **Changed**: `rendering/geometry/cluster_lod/enabled` defaults to false
  (D041, maintainer's request). The large-world test project turns it on.

## 2026-11-27 (valley test after G1)

* **Fixed**: the valley smoke (`forest_map.gd`) failed since the HZB
  occlusion of render list meshes (G1, 2026-11-24): its "disabled" mode
  left `mesh_occlusion/enabled` and `gpu_instances/occlusion` on, so the
  HZB was still built. It now turns them off too. Passes on Vulkan, D3D12
  and night.

## 2026-11-27 (TLAS kept in static frames)

* **Added**: [TLAS static skip](/project/tlas-static-skip.md): the TLAS is
  built only when the instances changed or a BLAS was rebuilt or refit
  (generation counter shared by all viewports). DDGI static interior AS
  stage 0.094 to 0.007 ms. TLAS refit (roadmap G8) is not done: NVIDIA
  recommends rebuilds.

## 2026-11-27 (cluster render buffer, light bench)

* **Added**: [cluster render buffer used size](/project/cluster-render-buffer-used-size.md):
  the intermediate cluster buffer is laid out and cleared for the frame's
  elements, not the limit; at `max_clustered_elements` 4096 and 128 lights
  the GPU frame went from 0.583 to 0.309 ms (Vulkan). New bench
  `devtools/test/light_bench.ps1` (`misc/light_bench_project`).

## 2026-11-27 (lights over the cluster limit)

* **Fixed**: [light cap](/project/light-cap-nearest.md): more visible omni,
  spot or area lights than `max_clustered_elements` dropped the last ones
  the cull listed (a near light could vanish); now the ones whose range is
  nearest to the camera are kept, with a one-time warning. Test
  `light_cap` (fails on the old code, passes on Vulkan and D3D12).

## 2026-11-27 (cluster LOD in shadow passes)

* **Added**: [shadow CLOD](/project/shadow-clod.md): shadow passes select
  their own CLOD cut (error in shadow-map texels), one compute batch per
  shadow batch, compact streams; settings `cluster_lod/shadows`,
  `cluster_lod/shadow_error_texels`. `shadow_clod_bench`: shadow stage
  0.86-0.93 to 0.08-0.10 ms, GPU frame 2.2-2.6x (Vulkan, D3D12). New
  large-world test `shadow_clod`; `clod_raster` pins shadow CLOD off.
* **Seen again**: [bench without the feature active](/mistakes/bench-without-the-feature-active.md):
  the first bench read `RENDER_TIMESTAMP` names and got zeros; it now
  reads direct `GPU Geometry Shadows` timestamps.

## 2026-11-27 (compact CLOD command streams)

* **Added**: [compact CLOD commands](/project/compact-clod-commands.md):
  `RenderingDevice::draw_list_draw_indirect_count()` and
  `SUPPORTS_DRAW_INDIRECT_COUNT` (Vulkan `drawIndirectCount`, D3D12
  `ExecuteIndirect` count buffer); CLOD selection and HZB kernels append
  only drawn groups. `hzb_bench` GPU frame 2.2-5.5x faster on Vulkan,
  1.6-3.3x on D3D12; setting `rendering/geometry/cluster_lod/compact_commands`.
* **Resolved**: [CLOD two-pass zero draws](/project/clod-two-pass-zero-draws.md).

## 2026-11-27 (DDGI flicker since 0.10.1)

* **Seen again**: [render graph finding](/ddgi/findings/render-graph-bounce-loss.md):
  DDGI probes flicker in spots since 0.10.1 (`flicker_px` 8-16%, baseline
  0.18%); `GODOT_RG_REORDER=0` or `GODOT_RG_FULL_BARRIERS=1` restore it.
  Release editors 0.8.1 and 0.9.1 clean. Fixed with a timestamp fence
  after `_ddgi_process()` (6 runs 0.19-0.26%, GPU cost unchanged); the
  untracked access is still open. Smoke fails above 1% `flicker_px`.
* **Added**: [sync points at labels](/successes/sync-point-at-labels.md).
* **Mistake**: [bisected before reading the KB](/mistakes/bisected-before-reading-the-kb.md).
* **Protocol**: a DDGI test run with `--frames` alone never quits; noted in
  [the protocol](/tooling/ddgi-test-protocol.md).
* **Smoke**: PhysX runs use `--disable-vsync`, and the PhysX example
  project is imported on first run (a fresh clone failed on `class_name`
  parse errors).

## 2026-11-27 (stock 4.7.2 vs the fork)

* **Added**: [stock compare](/project/stock-compare-2026-11.md):
  `devtools/test/stock_compare.ps1`, `misc/stock_compare_project`
  (game-level and physics benches on stock 4.7 API), results in
  `docs/benchmarks/stock-compare.md`. Game levels 2.9-6.0x, instances
  1.1-15.9x, scene runtime up to 19x at 100k, PhysX GPU 2.6-2.7x over
  Jolt from 8k bodies, 2.1x slower at 2k; cold shader stall 10% longer.
* **Mistakes**: [engine limit capped the workload](/mistakes/engine-limit-capped-the-workload.md),
  [SDK build outside the developer shell](/mistakes/sdk-build-outside-the-dev-shell.md).
* **Seen again**: [single-arg splat](/mistakes/powershell-single-arg-splat.md):
  `-File ... -Suites a,b` arrived as one string; the runner now splits.

## 2026-11-26 (O3DE Atom renderer research)

* **Added**: [O3DE Atom renderer](/research/o3de-atom-renderer.md): frame
  graph transient aliasing, depth-binned tile light culling, CPU
  auto-instancing, job pipelining, shader variant fallback. Ideas
  (draft): depth occupancy mask for the cluster builder, transient pool
  for per-frame intermediates. Nothing measured.

## 2026-11-26 (mesh shader research)

* **Added**: [mesh shaders](/research/mesh-shaders.md): API limits,
  vendor meshlet sizing, culling efficiency, platform coverage, state of
  Godot PR 88934. Nothing measured; D002 unchanged.

## 2026-11-24 (built-in level streamer)

* **Added**: [built-in level streamer](/project/level-streaming.md)
  (`modules/level_streaming`: `LevelStreamer3D`, `LevelStreamCell3D`,
  gizmo, export plugin; engine hook
  `EditorExportPlugin::get_runtime_dependencies()`), setup in
  `docs/level-streaming.md`, acceptance fixture
  `misc/level_streaming_test_project` with `devtools/test/level_streaming.sh`.
  Exported release runs: 6 of 6 pass; streamed p99 1.35 ms vs 1.30 ms
  preloaded (within noise); boundary frames p99 7.6 ms, all in the
  teleport thrash; cold teleport ready in 25-34 ms.
* **Mistake**: [template older than the module](/mistakes/template-older-than-the-module.md).
* **Added**: level streaming doctests (4 cases, 51 assertions).
* **Fixed**: `test_hash_set.cpp` did not compile with GCC: a `uint64_t`
  key lambda returned `unsigned long long`. It now passes, 15 of 15 cases.
* **Found**: on Linux, `production=yes` means full GCC LTO, so every relink
  takes 10-20 min. Iterate with `lto=none` and keep LTO for measured and
  release builds.

## 2026-11-24 (two-pass depth for GPU path instances)

* **Added**: [two-pass depth for the GPU path](/project/mesh-occlusion.md#two-pass-depth-for-the-gpu-path-2026-11-24)
  (`rendering/geometry/gpu_instances/occlusion_two_pass`,
  `gpu_instances_depth_deferred`); `mesh_occlusion.tscn` covers both paths.
  G3 is complete: every path (CLOD, MultiMesh, GPU path, CPU list) has
  two-pass depth.
* **Seen again**: [separate runs had an order bias](/mistakes/separate-runs-order-bias.md):
  `instance_bench` run order alone moved the 100k cases by 1.2 ms.

## 2026-11-24 (two-pass depth for CPU list meshes)

* **Added**: [two-pass depth for CPU list meshes](/project/mesh-occlusion.md#two-pass-depth-2026-11-24-gpu-first-plan-g3)
  (`rendering/geometry/mesh_occlusion/two_pass`, `meshes_depth_deferred`),
  fixture `mesh_occlusion.tscn` with SSAO and a negative control;
  `multimesh_occlusion.tscn` got SSAO.
* **Changed**: CLOD viewport budget 2^20 groups and 16,384 draws (was 65,536
  and 4,096; only 55 of `hzb_bench`'s 225 objects fit).
* **Found**: [CLOD two-pass costs more than it saves](/project/clod-two-pass-zero-draws.md).

## 2026-11-24 (cluster LOD for shared meshes)

* **Fixed**: [cluster LOD skips repeat-batched elements](/project/clod-skipped-for-batched-elements.md)
  (GPU-first plan G5): the opaque list no longer batches CLOD-eligible
  surfaces; `hzb_bench` draws CLOD again, street grid 14-37% faster.

## 2026-11-24 (batched MultiMesh selection)

* **Changed**: [two-pass MultiMesh occlusion](/project/multimesh-two-pass.md):
  scan, scatter, split and phase-2 selection run as one dispatch for all
  draws; valley now 96-101% of one pass (was 101-107%), street grid 9-11%
  faster. `culling.gd` kernel test uses the batched protocol.

## 2026-11-24 (two-pass MultiMesh occlusion)

* **Added**: [two-pass MultiMesh occlusion](/project/multimesh-two-pass.md)
  (GPU-first plan G3, MultiMesh path):
  `rendering/geometry/multimesh_gpu_culling/occlusion_two_pass`,
  `multimesh_depth_instances` statistic, fixture `multimesh_two_pass.tscn`,
  `multimesh_occlusion.tscn` part E, `hzb_bench.tscn` mode `two_pass+mm2`.
* **Found**: [cluster LOD skips repeat-batched elements](/project/clod-skipped-for-batched-elements.md);
  `hzb_bench` draws no CLOD.

## 2026-11-24 (adaptive VRS)

* **Added**: [adaptive variable rate shading](/project/adaptive-vrs.md)
  (GPU-first plan G2): `Viewport.VRS_ADAPTIVE`,
  `rendering/vrs/adaptive_threshold`, `rendering/vrs/mode` defaults to
  Adaptive; fixture `vrs_adaptive.tscn`. Pixel-compare fixtures pin VRS off.
* **Added**: mistake [doctool with --gdscript-docs](/mistakes/doctool-gdscript-docs-flag.md).

## 2026-11-24 (Cluster LOD debug view)

* **Added**: [Cluster LOD debug draw mode](/project/resident-clod-raster.md#debug-view)
  (`Viewport.DEBUG_DRAW_CLUSTER_LOD`, editor Display Advanced > Cluster LOD).
* **Added**: [per-instance cluster LOD](/project/resident-clod-raster.md#per-instance-control)
  (`MeshInstance3D.cluster_lod`, `INSTANCE_FLAG_USE_CLUSTER_LOD`,
  `rendering/geometry/cluster_lod/mesh_instance_default`). Occlusion tests
  (`hzb`, `hzb_two_pass`, `multimesh_occlusion`, `gpu_instances`,
  `clod_raster`) pass on the Linux .NET editor, Vulkan.
* **Added**: [GPU occlusion for CPU render list meshes](/project/mesh-occlusion.md)
  (`rendering/geometry/mesh_occlusion/enabled`, default on).
* **Changed**: the `rendering/geometry/*` settings are registered in
  `RenderingServer::init()`, so they exist and show with every rendering
  method; cluster LOD setting descriptions rewritten.
* **Measured**: valley (`forest_map`), Linux Vulkan, RTX 3060: GPU 5.4 ms
  per frame (opaque colour 2.0, depth pre-pass ~1.1, shadows 0.87,
  post ~0.8); render CPU 0.5 ms. GPU-bound: next work is GPU-side
  (plan in `docs/renderer/roadmap.md`, "GPU-first plan").

## 2026-11-24 (DDGI infinite world)

* **Fixed**: [scrolled probes traced at another probe's position](/ddgi/findings/negative-modulo-scroll.md)
  (brown squares on floors with `infinite_world`).
* **Mistake**: [ruled out scrolling with a jump smaller than the probe grid](/mistakes/jump-smaller-than-the-grid.md).

## 2026-11-24 (sync, DDGI headless leak)

* **Synced** `dev`: PhysX module `aaafe7ed42`, upstream master
  `94fc09c62d` (MetalFX jitter ported into `_render_3d_upscaling`).
  Linux .NET editor builds; smoke (PhysX game and editor, path tracer,
  DDGI interior) passes.
* **Fixed**: [automatic reflections leaked an instance every frame on the dummy renderer](/ddgi/findings/headless-auto-reflection-leak.md).
* **Open**: NVIDIA's force-push `49a825dc52` is half merged on the
  `dev-nvidia` branch (43 of 57 files). Its rewrite of TLAS building
  conflicts with the fork's DDGI and path tracer changes in
  `render_raytracing.*` and the ray tracing shaders; those need porting,
  not hunk merging. In the merged `fog.cpp`, the ray-query shadow path
  ignores DDGI.

## 2026-11-17 (release 0.10.3)

* **Released**: `godot4.8-beta-diginaat-physx-v0.10.3` at `db5506be7d`
  (project manager warning instead of a forced backup, D040), editors and
  export templates, standard and .NET; smoke passed for both.

## 2026-11-17 (project manager warning)

* **Changed**: the project manager no longer forces a backup before this
  build first opens a project; it shows a "Your Project Can Break" warning
  with "Open Anyway" and "Cancel" instead (D040, replaces D021).
* **Mistake**: [GUI clicks on a shared desktop](/mistakes/gui-clicks-on-a-shared-desktop.md)
  captured more than the test window.

## 2026-11-17 (release 0.10.2)

* **Released**: `godot4.8-beta-diginaat-physx-v0.10.2` at `09b6ec558f`
  ([shader permutations](/project/shader-permutations.md): feature groups,
  per-shader skipped variants, pruned transparent precompiles, full-key
  pipeline maps, freed load buffers, SDFGI binding fix), editors and export
  templates, standard and .NET; smoke passed for both.

## 2026-11-17 (shader permutations, step 1)

* **Started**: shader and pipeline permutation work on
  `feat/shader-permutations`; audit, counters and benchmark in
  `docs/shader-permutations/memory.md`. Baseline: 120 materials, cold,
  stall the frame that adds them for 146 s (driver pipeline builds: 1577 s
  CPU); 22 of 28 scene shader variants per material are never used.
* **Fixed**: freed compiled shader bytes and the loaded Vulkan PSO cache
  after use (warm memory 353 to 101 MB, same speed).
* **Mistake**: [warm runs on a new exe](/mistakes/warm-runs-on-a-new-exe.md)
  measured NVIDIA's per-exe driver cache.
* **Done**: [shader permutations](/project/shader-permutations.md): feature
  groups, pruned transparent precompiles, full-key pipeline maps, per-shader
  skipped variants, SDFGI binding fix; cold stall 146 to 95 s (Vulkan), 239
  to 97 s (D3D12), warm 2.4 to 0.65 s, memory 353 to 84 MB.

## 2026-11-17 (release 0.10.1)

* **Released**: `godot4.8-beta-diginaat-physx-v0.10.1` at `c968713b0b`
  (GPU-driven instances on by default, scene runtime, core containers,
  PhysX module `847b2ca459`), editors and export templates, standard and
  .NET; smoke passed for both.
* **Mistake**: [an interrupted .NET build](/mistakes/interrupted-mono-build-shared-objects.md)
  left mono-configured objects that broke the next standard link.

## 2026-11-16 (PhysX module sync)

* **Synced**: `modules/godot_physx` to uno1982 `847b2ca459` (56 commits:
  groom hair simulation with an optional Alembic importer, Blast
  background fracture, XR lens effects, PXTGI fixes, icons). Five
  conflicts were the fork's `PS3DE::` enum names and doc wording; one
  include added for master's `node_3d_editor_viewport.h`. SDK pin
  unchanged. NVIDIA unchanged since the 2026-11-07 decision.

## 2026-11-16 (scene runtime: M9, complete)

* **Added**: [scene runtime optimization](/project/scene-runtime.md), the
  finished work's page. Base against final: instancing 2.2x / 10.7x
  (10k / 100k), churn 2.8x / 11.4x, `heavy` -41% at 100k, 100k processing
  nodes -44%; extension API identical; large-world suite and smoke pass.

## 2026-11-16 (scene runtime: M5 rest, not built)

* **Decision**: the remaining per-child allocations (children map
  elements, owned list elements) are about 2% of instancing and churn;
  the open-addressing map would change child notification order, a shared
  pool is not worth its global state. Nodes already have no signal mutex.

## 2026-11-16 (scene runtime: M7 skipped, M8 no change)

* **Finding**: 2D needs no counterpart of M2-M4: groups and the children
  cache are in `Node`, and `CanvasItem::_notify_transform()` already stops
  at invalid nodes; 2D rendering takes local transforms, so nodes stay
  invalid and moves stop at their origin (`parents2d` 100k: 0.10 ms).
  `heavy2d` and `parents2d` added to the scene benchmark.

## 2026-11-16 (scene runtime: M6)

* **Implemented**: the process loop prefetches the next nodes' cache
  lines; 100k processing nodes were about 115 ns each in DRAM misses.
  `native` (100k Timers) 10.9 to 6.5 ms, `script` 100k -5%.

## 2026-11-16 (scene runtime: M5)

* **Implemented**: a node's groups in an ordered vector instead of a
  HashMap (one allocation instead of three), and no `StringName` built per
  node in scene instancing. `instance` -20-24%, `churn` -8-10%; each node
  24 bytes smaller.

## 2026-11-16 (scene runtime: M4)

* **Implemented**: Node3D transform propagation stops at dirty children
  that a propagation walked since the last transform notifications
  (`SceneTree::xform_change_epoch`); no work per notification. `heavy`
  (MeshInstance3D, headless) op 100k 53.6 to 36.2 ms, 10k -12%; `wide`,
  `parents` within 1.3%.
* **Mistakes**: [renderer-bound frame time](/mistakes/renderer-bound-frame-for-scene-changes.md)
  (code layout moved windowed runs 2-11%) and
  [five builds of one milestone](/mistakes/iterated-builds-instead-of-design.md).

## 2026-11-16 (scene runtime: M3)

* **Implemented**: `SceneTree::remove_from_group` nulls the member's
  recorded slot instead of searching and shifting; readers compact the
  holes in order. `churn` 100k 47.5 to 9.05 ms, `instance` 100k 812 to
  247 ms; controls unchanged. With M1, groups no longer make node churn and
  instancing quadratic.

## 2026-11-16 (scene runtime: M2)

* **Implemented**: removing a child leaves a hole in the parent's children
  cache; the next reader compacts it in place instead of rebuilding and
  sorting it. `churn_read` (new scenario: reads after removals) -15-17%,
  the profiled node workload -6.4%; scenarios without reads unchanged.
* **Finding**: a new per-node field outside existing padding made `wide`
  at 100k nodes 9% slower; moved into a bool's padding, no cost.

## 2026-11-16 (scene runtime: Phase 0 and M1)

* **Started**: data-oriented SceneTree work on `feat/scene-runtime`:
  docs/architecture/hybrid-scene-runtime.md (three approaches, targeted
  fixes chosen), docs/implementation/scene-runtime-roadmap.md,
  docs/benchmarks/scene-runtime.md, docs/scene-runtime/MEMORY.md; scenario
  benchmark `devtools/test/scene_bench.ps1` (12 scenarios, 1k-100k nodes).
* **Finding**: groups make scene instancing and node churn quadratic (a
  linear duplicate check per added member, a search and shift per removed
  one); 100k-node trees are memory-bound (about 1.45 KB per Node3D).
* **Implemented**: M1, the duplicate check only in dev builds: scene
  instancing of 100k nodes per frame 2,002 to 798 ms, churn 95.5 to 46.4 ms.

## 2026-11-16 (core: DynamicBVH volumes in SSE, D014)

* **Implemented**: `DynamicBVH::Volume` in aligned 16-byte corners with
  SSE2 kernels on x86-64 single precision, bit-identical to the scalar code;
  first `DynamicBVH` unit test (brute force). Node churn -2.6% against a
  scalar control build. docs/core-modernization/phases/04-x86-simd.md.
* **Success**: [a control build of the same source](/successes/control-build-for-layout-effects.md)
  showed that a 3% instancing "regression" was code layout, not the change.

## 2026-11-16 (core: HashMap one block, not kept)

* **Tried**: `HashMap`'s two arrays in one block, as D013 did for
  `HashSet`. No measurable change on three workloads (differences of both
  signs within the spread); reverted. docs/core-modernization/phases/02-hashing.md.

## 2026-11-16 (core: one allocation per HashSet, D013)

* **Implemented**: `HashSet` keeps its index arrays and keys in one block
  (one allocation per table and resize instead of four);
  `DependencyTracker::update_end()` uses a `LocalVector`. Scene instancing
  12.58 to 11.69 ms per frame, node churn 14.84 to 14.59 ms; large-set
  microbench neutral. docs/core-modernization/phases/02-hashing.md.

## 2026-11-16 (core: CPU profiles, lock-free RID ownership checks, D012)

* **Added**: `devtools/profile/`, a user-mode stack sampler (no elevation)
  with inlined frames and summaries by directory, type and caller.
* **Finding**: [where engine CPU time goes in core code](/project/core-cpu-profiles.md):
  hash lookups 3-16% of the busiest thread; scene instancing 21% in RID
  checks, locks and refcounts and 14% in heap calls; the callers' patterns
  cost more than the container internals.
* **Implemented**: thread-safe `RID_Alloc::owns()` lock-free on x86-64
  (D012); scene instancing 13.83 to 12.37 ms per frame, node churn
  unchanged. Upstream's all-CPU version was reverted for an Apple-Silicon
  bug (godotengine/godot#114900).

## 2026-11-15 (core templates: keep only the measured wins)

* Dropped the packed storage and robin-hood index migration (numeric hash
  tables 1.3-2.6x slower, 50-57% more live bytes); the working tree was
  saved on a local backup branch first. [Decision](/project/core-template-storage.md).
* Kept the byte hash for String/CharString keys inside HashMap, AHashMap and
  HashSet: String-key hits 0.62x (16 characters) and 0.55x (128) of the old
  time, integer tables and memory unchanged, full unit suite passes.
  [Numbers](/project/core-template-benchmarks.md).
* Recorded the specialized copy-only LocalVector run: small copies neutral,
  256+ elements 2.6-5x faster; the earlier small-copy regression was the
  harness. Bulk copy stays. [Evaluation](/project/unique-array-evaluation.md).

## 2026-11-15 (GPU path: no command uploads, D039)

* **Implemented**: the GPU instance path clears its indirect commands on
  the GPU and the cull writes each filled command's index count; bucket
  data is uploaded only when it changes; shadow rows test visibility first.
  No GPU change on the game level beyond the spread; `unique_lod_2k` GPU
  0.44-0.46 to 0.41-0.43 ms. docs/renderer/benchmarks.md, D039.
* **Finding**: stage timestamps put the GPU path's remaining cost on the
  level in its draws (about 50-60 ns more per draw in depth-only passes),
  not in the culls (camera below 0.01 ms, a shadow batch about 7 us). Not
  the indirect draw either: direct draws with read-back counts (temporary
  build) gained at most 0.03 ms. HZB occlusion does not run on the level
  (MSAA 2x).
* **Updated**: [D3D12 copy mistake](/mistakes/d3d12-buffer-copy-then-compute-write.md):
  a GPU clear before compute writes works there.
  [Mistake](/mistakes/fix-before-locating-the-cost.md): built the fix
  before timing the stages.

## 2026-11-15 (GPU path on game levels, D038)

* **Implemented**: the GPU path draws only the commands a listed member can
  fill (CPU copy of the cull's LOD choice, walked when the instances are
  not many more than the commands), per-pass shadow stats, and
  `gpu_instances/shadow_min_casters` (small shadow passes on the CPU list).
  Game level loss 0.27-0.30 to 0.05-0.12 ms; new bench case
  `unique_lod_2k`. docs/renderer/benchmarks.md, D038.
* **Implemented**: all shadow passes of a render culled in one dispatch
  (`MODE_SHADOW_BATCH`); the level is +0.04-0.08 ms, inside the A/A floor.
* **Finding**: the loss was the shadow passes' fixed cost (omni cube
  faces), not the empty draws the earlier entry suspected.
  [Mistake](/mistakes/separate-runs-order-bias.md).

## 2026-11-15 (GPU path on a real level)

* **Finding**: in a game level with many unique meshes and few copies each,
  the GPU instance path doubles the draw count and costs 0.15-0.3 ms of GPU
  time against the CPU path (3 alternating pairs). Recorded under known
  problems in docs/renderer/MEMORY.md; the level now sets
  `gpu_instances/enabled=false`.

## 2026-11-15 (moved instances)

* Render-side cost of a moved instance measured at about 510 ns (dirty
  loop, `dynamic_20k`); quantizing from the float exponent and no AABB
  recompute on transform-only changes cut it 9%. Rest is memory-bound; the
  main thread's share is larger and next. See docs/renderer/benchmarks.md.

## 2026-11-15 (GPU-first renderer: material parameter tables, D036)

* Forward+ material parameters moved from one UBO per material to per-shader
  storage-buffer tables with shared uniform sets; `unique_10k` render CPU
  7.5-8.1 to 0.42-0.47 ms, draws 8,776 to 11, D3D12 video memory 734-822 to
  130.7 MiB. [Finding](/project/material-parameter-tables.md).
* Startup crash during the work found with a map file and dumpbin, no
  symbol build. [Success](/successes/map-file-crash-location.md).

## 2026-11-10 (LocalVector threshold boundary follow-up)

* Tools-only extension: copy-only 64/128/255/257/512 neighborhoods, independent
  output directory, automated allocation/capacity parity and embedded manifests.
* Twelve sizes / eleven alternating process pairs pass contracts and checksum/
  allocation checks. Small copies regress ~9–33% with close STL controls; 256+
  copies improve. Performance acceptance blocked despite existing 279-test
  correctness pass. No engine edit, threshold retuning or new engine-wide claim.
* Corrected copy retained-byte reporting only in the untimed count binary;
  historical zero fields were omissions, not measured zero memory. Recorded
  [metric lesson](/mistakes/copy-benchmark-retained-bytes.md) and
  [all results/caveats](/project/unique-array-evaluation.md).

## 2026-11-10 (LocalVector copy checkpoint validation)

* MSVC production editor build with tests=yes completed successfully in 15m09s.
  Focused LocalVector/Vector/Array/Dictionary/hash/String/Variant selection:
  279 tests, 3,803,093 assertions, zero failures.
* Checkpoint only the LocalVector trait-aware bulk-copy path (>=256 elements),
  its regression tests and validation records. Layout, allocator and storage
  remain unchanged; inherited hash/String/Mono/COW experiments stay unstaged.
* Existing 11-pair copy measurements support 256/4K integer-copy improvements;
  small-copy overhead, 32K control confounding and outstanding platform/threshold
  gates remain explicit. No engine-wide or complete Phase 1 acceptance claimed.
  See [evidence](/project/unique-array-evaluation.md).

## 2026-11-10 (core modernization Phase 1 unique arrays)

* Added baseline-only LocalVector/std::vector-Godot-allocator evaluation: 56
  paired operations/sizes, basic contracts and untimed CRT allocation-event/peak
  instrumentation separate from timing. No allocator/storage backend change.
* Isolated LocalVector Span-copy candidate: use existing trait-aware bulk helper
  for >=256 trivial elements; preserve small/nontrivial copy loops. Added lifetime,
  no-default-constructor, independence, empty-span and bulk-path regression tests.
* Eleven alternating copy-only process pairs: 256 ints 126.39 -> 52.70 ns,
  4K ints 1524.79 -> 347.40 ns; allocation counts/capacities/bytes identical.
  Empty/single-copy extra cost and 32K STL-control confounding recorded explicitly.
  Editor build/test gate running; candidate not promoted. See
  [evidence](/project/unique-array-evaluation.md).

## 2026-11-10 (core modernization Phase 0 checkpoint)

* Created `feat/core-stl-simd-modernization` from `dev-stl-core` at
  `172e099b95307708005dafcf719693d20b1e7088`, preserving inherited dirty work.
* Added `docs/core-modernization/` architecture, compatibility, benchmark,
  decisions, persistent memory and six phase records. User-supplied Ankerl 2019
  methodology reviewed; engine-owned hashmap is primary Phase 2 candidate.
* Phase 0 benchmark isolation passed: original HEAD source snapshot plus only
  benchmark files and configured generated profiling header builds both control
  executables; all 24 container workloads pass. Math finite checks pass and
  reproduce slower explicit SSE dot/cross. No engine implementation accepted.
* Existing prototype implementation, vendor changes and prior KB records remain
  outside the Phase 0 commit. See [audit](/project/core-modernization-audit.md).

## 2026-11-10 (core template storage migration)

* Replaced former shared buffer implementation with packed `FlatSharedBuffer`;
  preserved String/Vector handle layout and COW, used STL lifecycle/algorithms
  and character traits. Mono reads shared layout constants instead of copies.
* Migrated HashMap/AHashMap/HashSet lookup to a pinned robin-hood index, retaining
  stable nodes or dense indices as required and handling full hash collisions.
* Added internal wyhash/XXH3 policy; preserved public legacy hash helpers and
  default-hasher outputs because API fingerprints and public type hashes use them.
* Regression tests added; MSVC C++17 syntax check passed. Isolated editor build
  and runtime tests pending. No speed/memory claims. See
  [compatibility boundary and validation](/project/core-template-storage.md).
* Standalone MSVC integer-mixer check matched pinned upstream `wyhash64` for
  1,000,000 deterministic value/seed pairs.
* Added 24-case actual-container A/B benchmark, baseline read from `HEAD`
  without checkout changes. Both executables pass correctness/allocation-cleanup
  checks. Timing collection correctly refused while the editor rebuild runs;
  contended probe timings discarded. Controlled samples pending.
* Narrowed standalone String linking to verbatim required methods after the
  full translation unit pulled unrelated engine services; updated the
  [translation-unit lesson](/mistakes/new-translation-unit-assumptions.md).
* **Bench completed:** 24 cases, 11 alternating baseline/candidate pairs on Ryzen
  5 3600, MSVC C++17 `/O2 /MT`, no overlapping compiler load. Integer AHashMap hit
  8.76 -> 22.67 ns (2.59x slower), churn 2.64x slower; numeric hash-container live
  allocations +50–57%. UTF-32 String-key lookup at 128 characters 182.54 ->
  111.56 ns; 64 KiB byte hash 19.74 -> 2.29 us. This is a microbenchmark, not a
  frame-time result. [Full conditions, p50/p95 and memory](/project/core-template-benchmarks.md).
* **Correctness:** isolated production editor build passes. Fixed duplicate
  upstream wyhash prime-helper symbols by namespacing only the reference-test
  copy; production benchmark code unchanged. Migration tests: 9 / 3,749,785
  assertions; focused core selection: 269 / 3,801,958 assertions, all passed.
  Full suite/rendering/.NET/sanitizers not run. Do not publish as a performance
  upgrade; container indexing needs redesign.
* Recorded [actual-container A/B workflow](/successes/actual-container-ab-bench.md)
  and the reference-header ODR lesson; no production performance fixes or
  benchmark-driven tuning applied during this measurement task.
* Focused compilation caught Zstandard's disabled XXH3 and a wrong constexpr
  expected value; recorded [thirdparty configuration lesson](/mistakes/bundled-xxhash-without-xxh3.md).


## 2026-11-10 (GPU-first renderer, Phase 0)

* **Started**: GPU-driven Forward+ work on `dev-gpu-renderer`, tracked in
  docs/renderer/ (audit, capability matrix, roadmap, bench protocol) and
  D025. New many-instance CPU bench `devtools/test/instance_bench.ps1`.
* **Finding**: baseline is render-thread CPU bound; at 100k nodes 16.4 of
  20.4 ms is main-pass render-list preparation, the cull 1.2 ms, GPU 1.15 ms
  (docs/renderer/benchmarks.md).
* **Implemented**: GPU instance table (persistent per-instance records,
  dirty uploads with a compute scatter, validation), opt-in, tasks 1.1-1.4.
* **Decision**: D027. The per-element CPU walk (about 63 ns per element,
  pointer chasing) dominates `_fill_instance_data`, so slot-ID elements were
  dropped; eligible instances will leave the CPU camera lists (Phase 2).
* **Implemented**: GPU path for eligible instances (Phase 2.1-2.4, D028),
  opt-in: draw buckets, compute cull writing instance data, one indirect
  draw per bucket. Images identical to the CPU path on Vulkan and D3D12;
  `massive_100k` render CPU 22.0 to 5.5 ms.
* **Implemented**: index LODs on the GPU path (Phase 2.5, D029): per-instance
  LOD in the cull kernel with the CPU rule, a visible-ID list per bucket
  level (D3D12 has no base instance). Meshes with LODs are now eligible;
  per-level counts and images equal to the CPU path on Vulkan and D3D12.
* **Implemented**: meshes with a separate shadow mesh on the GPU path: the
  depth pre-pass draws through twin indirect commands with the shadow
  mesh's index counts.
* **Implemented**: views with a motion pass (FSR 2, DLSS, MetalFX) on the GPU
  path (D030); `gpu_instances.ps1` compares TAA and FSR 2 motion vectors
  with the CPU path through a velocity readback.
* **Finding**: motion vectors of the same draws can differ only in the sign
  of a zero between pipeline specializations on D3D12; compare them as
  numbers. Equal depths of a moving and a resting instance let the draw
  order pick the vector, so such tests keep movers clear of other geometry.
* **Implemented**: viewport render info (objects, primitives) for GPU-drawn
  instances: workgroup sums in the cull pass, read back per view; equal to
  the CPU lists' counts (`instance_bench` now reports 83,924 objects at 100k).
* **Implemented**: GPU path buckets updated in place (D031): joins, leaves
  and material changes without a rebuild; new bench case `churn_100k`.
* **Fixed**: a crash on quit in about 70% of `gpu_instances.ps1` runs: the
  per-frame render-info readback completed after the renderer was gone.
  Added [readback callback after renderer teardown](/mistakes/readback-callback-after-renderer-teardown.md).
* **Implemented**: the GPU path draws what the scene cull found visible
  (D032): slot bits from the cull threads, so occluders apply. Building the
  bits from the pointer list on the render thread cost 2.7 ms at 100k; from
  the cull's slot list 0.2 ms.
* **Implemented**: directional shadow casters on the GPU path (Phase 6.1,
  D033): `massive_100k` render CPU 5.2 to 2.8 ms, `shared_20k` 4.3-5.5 to
  0.7-0.8 ms. A view with many buckets and few listed instances skips the
  empty buckets (`unique_10k` shadow draws 30,003 back to the CPU count).
* **Implemented**: spot, dual paraboloid and cube omni casters on the GPU
  path (Phase 6.2).
* **Finding**: when the shadow list's instance buffer grows mid-frame,
  `_fill_instance_data` copies all elements again from the first, which
  turned earlier passes' GPU path elements into CPU draws (only seen with
  enough passes: a dual paraboloid omni light made it show). The copy now
  keeps them. Found by isolating the light type, then looking at where the
  images differed (the sun's shadows everywhere, not near the omni light).
* **Implemented**: shadow render info for GPU-drawn casters (Phase 6.3).
* **Changed**: shadow passes draw from the camera view's instance data (the
  camera kernel writes every live member): buffers at 100k 147.5 to 125.5 MiB.
* **Changed**: the motion age-out visits a list of instances with motion
  instead of every listed instance (camera, RT and GPU path):
  `massive_100k` render CPU 2.8-3.2 to 1.5-1.6 ms on the GPU path, about
  1 ms lower on the CPU path.
* **Implemented**: HZB occlusion of GPU path instances for the colour passes
  (D034, `rendering/geometry/gpu_instances/occlusion`, opt-in).
* **Fixed**: shadow render info counted earlier passes' elements again when
  the instance buffer was remapped mid-frame (upstream has the same order).
* **Changed**: the GPU geometry paths (GPU path and its occlusion, table,
  MultiMesh GPU culling, cluster LOD, streaming) are on by default (D035).

## 2026-11-08 (release 0.9.1)

* **Updated**: release 0.9.1 (weather and wind, shadow speed-ups) in
  CUSTOM_BUILD.md; README.md lists the weather, the performance work and a
  Credits section.
* **Updated**: the engine calls itself "Godot Diginaat" (window titles,
  startup header, version button), set in `methods.get_version_info()`.

## 2026-11-08 (render speed: shadow LODs, grass)

* **Implemented**: [coarser mesh LODs for shadow
  casters](/project/shadow-lod-scale.md)
  (`mesh_lod/lod_change/shadow_threshold_scale`, default 4): valley shadows
  2.02 to 1.62 ms. `GrassField3D` no longer captures a timestamp per field.
* **Finding**: a real level's grass costs 3.3 ms (of 7), pixel-bound:
  alpha-tested overdraw inside Megascans clump meshes and the lit colour
  pass. Near-first patch order and gating frame-level timestamps did not
  help (recorded on the same page).

## 2026-11-08 (render speed: directional shadow cascade cache)

* **Implemented**: [far directional cascades kept between
  frames](/project/directional-cascade-cache.md)
  (`directional_shadow/far_cascade_update_interval`, default 4). Valley
  shadows 3.53 to 2.03 ms held, 2.53 to 1.41 ms flying; whole frame -1.1
  to -1.5 ms.
* **Tooling**: `gpu_intervals.ps1 -FixedFps` (frame-locked moving cameras)
  and the valley's `--view-time=T` (one held view, every flying feature on).

## 2026-11-08 (stability: separate render thread shutdown)

* **Fixed**: with the separate render thread, the main RenderingDevice
  stayed bound to the ended server thread, so `finalize()` failed its thread
  guard at exit ("can only be called from the render thread") and skipped
  the shutdown; two ObjectDB instances leaked. `RenderingServerDefault::finish()`
  now makes it current on the main thread again. A real level exits clean.

## 2026-11-08 (render speed: rain collision)

* **Fixed**: the rain's collision height field moved with the camera-following
  root every frame, and any move re-renders it: 0.25 ms GPU per frame in a
  real level, even with a still camera. It now follows in grid steps (0.02
  ms average). Checked: rain still stops on a roof after walking 20 m. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-08 (render speed: GPU voxel field)

* **Fixed**: `VoxelField3D` captured a GPU timestamp per field, which
  serialized the render graph: 149 fields in the valley took 3.1 ms of GPU
  selection, now 0.5 ms. Whole frame 10.4 to 7.5 ms on Vulkan.
* **Tooling**: `devtools/test/gpu_intervals.ps1` (whole-frame GPU time per
  timestamp interval).
* **Mistake**: [the viewport's GPU time missed pre-viewport
  work](/mistakes/viewport-time-misses-pre-viewport-work.md); the far-field
  cost figures were corrected in [its page](/project/far-field-gpu-voxel-levels.md).

## 2026-11-08 (weather sky material)

* **Implemented**: `WeatherSkyMaterial`, the built-in weather sky as a
  `ShaderMaterial`, so it shows in a sky's material list. The shader is
  served from memory at `res://.builtin/weather_sky.gdshader`; saved
  materials refer to it by path.
* **Finding**: a constructor that sets object properties (a shader, noise
  textures) makes ClassDB keep them as default values until exit, after
  the RenderingServer is gone: leaked RIDs and an "Instantiated ... used as
  default value" warning on save. Fix: `ClassDB::set_property_default_value()`
  with null for those properties. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-08 (rain emission)

* **Implemented**: `WeatherRainEffect.emission`, a self-lit glow of the
  drops in the rain colour (default 0.5), so rain stays visible at night
  and where the refracted background hides it. Checked on Vulkan in a dark
  storm at 0, 0.5 and 1.5. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 8: wind zones)

* **Implemented**: `WindZone3D` (directional, radial, vortex; sphere or
  box; add or replace), zones in `WindServer.sample()` and the wind shader
  include through a float texture, `WindServer.sample_global()`, an
  editor gizmo. 22 checks; GPU matched CPU within 0.031 m/s. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 7: sound)

* **Implemented**: `WeatherSoundEffect` and `WeatherSoundSynth`: rain,
  wind and thunder synthesized in C++ on worker threads (no audio files),
  crossfaded rain, wind by speed, thunder delayed by distance through a
  low-pass and reverb bus. 22 checks. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 6: lightning)

* **Implemented**: `WeatherLightningEffect` (return-stroke flashes lighting
  the scene and the sky, branched bolts, Strike Lightning button) and
  `Weather.lightning_struck`. Fixed: the sun search took internal lights.
  20 checks headless and on Vulkan. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 5: haze and motes)

* **Implemented**: `WeatherHazeEffect` (volumetric ground mist, rain shafts
  and spray in a camera-following `FogVolume`), `WeatherMotesEffect`
  (floating droplets) and `Weather.get_effect()`. 16 checks headless and on
  Vulkan. See [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 4: rain)

* **Implemented**: `WeatherRainEffect` (GPU drops and impacts around the
  camera, collision height field, top-down rain occlusion map) and the
  built-in wetness include and wet surface shader, served from memory and
  left out of exports. Wet ground darkened to 0.77 on Vulkan while the
  ground under a roof stayed dry. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 3: sky)

* **Implemented**: `WeatherSkyEffect`: drives a weather-ready sky material
  in place, or shows the built-in weather sky (shader kept as a C++ string,
  noise textures built in code) in place of the scene's while it runs.
  Clouds drift with the wind aloft. 17 headless checks and Vulkan
  screenshots. See [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 2: core)

* **Implemented**: `WeatherProfile`, `WeatherLibrary` (11 built-in types),
  `Weather` node (blending, wind driving, wetness globals, borrowed scene
  values restored before save), `WeatherEffect` resources and
  `WeatherLightingEffect`. 24 headless checks pass. See
  [World wind and weather core](/project/world-wind.md) and `WEATHER.md`.

## 2026-11-07 (weather step 1: wind)

* **Implemented**: `modules/weather/` with the `WindServer` singleton (gusts
  that travel with the wind, same noise in C++ and in the built-in shader
  include `res://.builtin/wind.gdshaderinc`), `WindReceiver3D` (drag on rigid
  bodies), wind in `GrassField3D`'s default material and
  `PhysXCloth3D.use_global_wind`. GPU matched CPU within 0.043 m/s. See
  [World wind](/project/world-wind.md) and `WEATHER.md`.

## 2026-11-07 (sync)

* **Updated**: CUSTOM_BUILD.md sync log: upstream `b47a795aa5`, physx module
  `92da57bda8`. PXTGI needed `node_3d_editor_viewport.h` (master split it out
  of the 3D editor plugin). Smoke: PhysX GPU, path tracer and DDGI pass.
* **Decision**: NVIDIA force-pushed `nvidia-pt-dlss` (`49a825dc52`, same base,
  about 28k changed lines). Not merged: against the old tip it conflicts in
  53 files (130 hunks, mostly the path tracer and the renderer interface,
  where this fork added planar reflections and DDGI). The fork keeps NVIDIA
  `135dff3887`. A later merge should use `135dff3887` as the base
  (`git merge-tree --merge-base=135dff3887`), not the upstream merge base.

## 2026-11-07 (popup corner transparency)

* Follow-up reproduced the remaining rectangle: native Vulkan composition added
  the GDI early-clear RGB behind an already zero-alpha popup texture. A black
  GDI erase experimentally restores exact background pixels; patched
  `WM_ERASEBKGND` for transparent windows. Production rebuild passed; Vulkan and
  D3D12 each verified 24 zero-alpha pixels with zero background mismatches.
  User confirmed the fix works.

* Enabled editor/project-manager per-pixel transparency independently of project
  settings; Modern Compact menus now retain rounded corners without a theme shadow.
  See [theme notes](/project/modern-compact-editor-theme.md). Production build and
  Vulkan/D3D12 project-manager startup passed; user confirmed popup appearance.

## 2026-11-07 (split workspace and Live Runtime view)

* **Implemented**: second main screen panel (`DOCK_SLOT_MAIN_SCREEN_SPLIT`) so
  the Game view and the 2D/3D editor show at once; Live Runtime toggle in the
  2D/3D toolbars that streams changed transforms and visibility from the game
  over the debugger and applies them to rendering server instances, with
  ghosts for spawned instances. Design, evaluation of a runtime camera,
  limits and costs in `SPLIT_WORKSPACE.md`.
* **Validated**: scratch 3D (rigid bodies, spawner) and 2D (parented sprites,
  despawning bullets) scenes, play/stop cycles, hidden views, smoke tests.

## 2026-11-07 (transparent popup perimeter)

* **Refined**: reserve 2px fully transparent padding around compact popup menus,
  panels and tooltips using existing shadow insets, with no visible shadow.
  Rounded antialiasing no longer touches the native window boundary; fill,
  radius and item spacing stay unchanged.
* **Evidence**: prior popup readback had transparent corner pixels but opaque
  straight-edge pixels at the window boundary. Add a full-perimeter alpha check,
  rather than treating one transparent corner pixel as sufficient proof.
* **Validated**: production build and OpenGL/Vulkan/D3D12 checks passed. The
  complete outer popup perimeter now has zero alpha in both compact presets;
  dock defaults, floating tabs and switch stability still pass. Project Manager
  and unchanged game-default checks pass. Desktop visual confirmation pending.

## 2026-11-07 (compact header and gray theme)

* **Implemented**: darker compact header, selectable Modern Compact Gray with
  untinted surfaces, and clearly bounded inactive scene/dock tabs. Shared compact
  geometry and approved opaque antialiased outlines remain unchanged.
* **Padding correction**: main-screen switches share identical normal/hover/
  selected/disabled margins and consistent gaps; selection no longer shifts content.
  Verified failing margin checks before the patch, then passing equal-margin and
  live fixed-tab-rectangle checks in both compact presets after rebuilding.
* **Inactive tabs**: removed outlines from unselected/hovered/disabled tabs,
  including inner tab variants; selected-tab outlines and fixed spacing remain.
  Rebuilt and passed live checks for zero inactive borders on all four sides,
  retained selected borders, and unchanged main-screen switch rectangles.
* **Active marker**: user requested a neutral 2px top edge on selected tabs;
  shared treatment covers scene/dock, main-screen and bottom-panel tabs without
  adding colored toolbar lines or changing content margins.
  Production build and live checks passed: neutral 2px markers, borderless
  inactive states, fixed switch rectangles, both compact presets and Default.
  **Reverted after visual feedback**: user rejected the marker. Restore prior
  selected-tab styling only; keep borderless inactive tabs and fixed padding.
* **Floating tabs**: slightly darker inactive fill, four rounded corners and a
  2px vertical drawing inset with 4px gaps at Compact spacing. Layout/content
  margins remain unchanged; inactive tabs stay borderless, with no top marker.
  Production build and live checks passed for both compact presets and Default;
  verified rounded corners, insets, darker inactive fill, borderless inactive
  states and stable switch rectangles. Captured editor screenshot inspected.
* **Rounded popups**: context menus/submenus, popup panels and tooltips share
  compact themes' smooth 6px corners and opaque 1px outlines. Other presets and
  native OS window decorations remain unchanged.
* **Contrast**: brighter text, darker inputs, lighter elevated/button surfaces
  and slightly stronger opaque neutral outlines; width and density unchanged.
* **Dock defaults**: Text and Icon for dock and bottom-dock tabs, matching the
  desktop main-screen default. Saved preferences and Android's main-screen
  default are preserved.
* **Tab height**: scene/dock and inner tabs gain 2px padding above/below;
  floating insets, widths and main-screen switch padding stay unchanged.
* **Black popup corners**: enable layered windows only for editor/Project Manager
  startup, independent of the game's transparency setting. Transparent popup
  image corners alone did not prove native composition was enabled.
* **Validated**: production build; live compact/gray/Default checks passed on
  OpenGL, Vulkan and D3D12, including native transparency availability, popup
  alpha, taller tab padding and stable switch rectangles. Project Manager startup
  passed; a separate game run confirmed transparency remains disabled by default.
  User confirmed final desktop appearance, including popup corners.
* **Validated**: rebuilt production editor; both compact presets and Default
  passed live checks, including dock revert defaults, increased text contrast,
  popup corner geometry/transparent outer pixels, inactive tabs and stable main
  switches. Rendered popup screenshots for both compact presets inspected.
* **Scope**: native editor theme only; no control or runtime UI changes.
* **Validated**: Windows production build; live Modern Compact / Gray / Default
  switching and generated style assertions passed on OpenGL Compatibility at
  100% scale. Both compact editor screenshots inspected. Other scales and
  renderer/physics smoke tests were not run for this theme-only refinement.
* **Test harness correction**: use `EditorInterface.get_editor_theme()` rather
  than a Control's local (possibly null) theme; record script errors separately
  from the bounded editor process's exit status.

## 2026-11-07 (MultiMesh HZB occlusion and the HZB bench)

* **Implemented**: `rendering/geometry/multimesh_gpu_culling/occlusion`
  (D024): instance HZB test after the depth pre-pass, colour passes only;
  shared HZB setup/build; statistic `hzb_instances_occluded`; always-on stage
  timestamps while GPU geometry is active.
* **Measured**: `hzb_bench` city scene, four modes, Vulkan and D3D12: instance
  occlusion -0.4 to -0.6 ms opaque; CLOD two-pass a net +0.2-0.3 ms here.
* **Mistakes**: D3D12 buffer copy then compute write; a bench whose shared
  mesh and material batched every CLOD object out of CLOD (now self-checked).

## 2026-11-06 (CLOD HZB occlusion under jitter)

* **Fixed**: HZB occlusion no longer switches off under TAA, FSR2 or DLSS
  jitter; the test already used the jittered projection (D023).
* **Validated**: `hzb_two_pass` TAA and FSR2 cases (bit-equal images, groups
  occluded), forest TAA A/B 0 px, on Vulkan and D3D12.

## 2026-11-06 (two-pass CLOD HZB occlusion)

* **Implemented**: `rendering/geometry/cluster_lod/occlusion_two_pass`: split,
  test and phase-2 depth with a per-viewport group history, after a reference
  engine's culling (D023); four new statistics words.
* **Validated**: `hzb` kernel fixture (two-pass rounds), new `hzb_two_pass`
  scene fixture on Vulkan and D3D12, full large_world suites and smoke.

## 2026-11-06 (GrassField3D level of detail rings)

* **Implemented**: `GrassField3D.inner_radius`, `inner_fade`, `edge_dither`:
  fields with the same seed hand each blade over between a detailed near
  ring and a card ring (D022).
* **Validated**: `grass_field` fixture LOD ring phase on Vulkan and D3D12
  (2551 + 3447 = 5998 blades, same origins as one field).
* **Measured**: grass test scene at 1080p 10.2 to 7.6 ms GPU.

## 2026-11-06 (paged-out cluster indices, backup before opening)

* **Implemented**: `MeshClusterData.save_pages(path, keep_indices)` and
  `are_indices_paged()`; with `keep_indices` false the resource holds only the
  roots' indices and the renderer reads the rest from the page file, or the
  whole file without streaming (D020). Scene import uses it.
* **Implemented**: the project manager requires a backup before this build
  opens a project the first time, showing project size and free disk space
  (D021).
* **Validated**: `import_paged` extended (paged-out record checks, file-read
  resident copy, missing file with streaming on and off) and all 16
  large_world fixtures on Vulkan and D3D12. The backup dialog builds; it was
  not clicked through yet.

## 2026-11-06 (CLOD page files from scene import)

* **Implemented**: import option `meshes/cluster_lod_page_files`; scene import
  writes one `.clodpages` file per cooked mesh into the imported folder before
  saving and lists it as a generated file, so export takes it along (D019).
  Idea from CRYENGINE 5.7's split of a mesh into a header and a stream-only
  geometry file (ideas only, no code).
* **Validated**: new `import_paged` fixture and the full large_world suite on
  Vulkan and D3D12; standard smoke passes.

## 2026-11-06 (modern compact editor theme)

* **Font changed**: editor UI font is now Atkinson Hyperlegible Next 2.66
  (OFL), with Inter kept as first fallback for Greek, Cyrillic and symbols.
  Ubuntu Sans was reviewed and not bundled (Ubuntu Font Licence, not OFL).
* **Typography research**: recommend existing embedded Inter UI and JetBrains
  Mono code fonts; source confirms both already implemented, with user overrides
  and international fallbacks. No font replacement required.
* **Refined**: user-approved opaque antialiased outlines; Avalonia-inspired
  opaque surface hierarchy, neutral hover/pressed buttons and muted-blue selection.
  Icon saturation increased to 1.25 following visual feedback.
* **Implemented**: native Modern Compact editor color preset and Compact default
  spacing, using neutral charcoal surfaces, restrained blue interaction states,
  colored native icons, small corner radii, subtle tree hierarchy guides and
  low-contrast one-logical-pixel neutral outlines. Selected tabs use a raised
  neutral surface and outline rather than decorative accent lines.
* **Scope**: shared editor theme settings and generation only; no editor control,
  layout, runtime project theme or plugin behavior changes.

## 2026-11-06 (GPU voxel levels for the far field)

* **Implemented**: `VoxelFieldData`, `VoxelProxyBuilder.build_field` and
  `VoxelField3D`, voxel levels with per-brick level selection in a
  compute pass that fills an indirect MultiMesh (D018). Splats are round discs,
  for the static proxies too. The valley flies with GPU levels (64 voxels,
  2 px).
* **Validated**: new `voxel_field` fixture (GPU selection exactly matches the
  CPU at 300-4000 m and orthogonal, frustum, cap, coverage and colour) and the
  full large_world suite on Vulkan and D3D12. Smoke passes.
* **Measured**: image error against the source meshes 3.69 against 5.13 for
  the static proxies; +0.71 ms GPU in the widest valley view. The first bench
  loop measured static in both modes (args dropped by `powershell -File`;
  mistake updated).

## 2026-11-06 (CLOD pages from disk)

* **Implemented**: `MeshClusterData.save_pages` / `clear_pages` /
  `get_page_file` write and record a checked `.clodpages` file; CLOD streaming
  then holds only root indices and reads other groups on worker threads,
  dropping reads of released surfaces (D017).
* **Validated**: new `streaming_disk` fixture (disk convergence, pool rebuild,
  damaged and stale files, free with reads in flight) on Vulkan/D3D12.
  Found and fixed: finished reads were not collected once no streamed surface
  was left.

## 2026-11-06 (GPU grass, far-field cross-fade)

* **Implemented**: `GrassField3D`, GPU-generated grass in a compute-filled
  indirect MultiMesh (hashed blades, height and density grids, frustum-culled
  patches, continuous fade); the valley's meadows use it (key B,
  `--gpu-grass`). The far-field switch cross-fades over 8 m with Godot's
  visibility-range fade (`--far-hard-cut` keeps the old switch). The streaming
  fixture checks the streaming-memory monitor.
* **Validated**: new `grass_field` fixture (exact blade count, heights exact on
  a non-linear triangulated grid, density mask, frustum bounds, budget cap,
  coverage) and all large_world suites, dense forest and valley day/night on
  Vulkan/D3D12.
* **Measured**: grass +0.21-0.45 ms GPU for 72-93k blades (D015); cross-fade
  costs nothing measurable against the hard cut (D016).

## 2026-11-06 (CLOD paging, voxel far field, distance thinning, monitors)

* **Implemented**: opt-in in-memory CLOD paging (one page per cluster group in a
  budgeted GPU index pool, pinned roots, async feedback, LRU eviction);
  `VoxelProxyBuilder` far-field splat proxies, used for valley patches beyond
  180 m; `MultiMesh.distance_thinning_*` applied in the GPU culling kernel;
  Performance monitors for GPU-culled instances, CLOD triangles, HZB-occluded
  triangles and CLOD streaming memory.
* **Validated**: new `streaming` and `voxel_proxy` fixtures and a thinning phase
  in `multimesh_lod`; all large_world suites, dense forest and valley day/night
  on Vulkan/D3D12; standard smoke.
* **Measured**: far field saves 0.23-1.0 ms GPU (7-16%) per valley view,
  repeated on Vulkan and D3D12 (D013). Thinning draws 13-23% fewer instances
  with no GPU change in the valley (D014). Streaming has no timing yet (D012).
* **Mistakes recorded**: splats wound as back faces lost direct light under
  `cull_disabled`; a pressure budget taken as a fraction of the cooked size never
  constrained the test views.

## 2026-11-06 (shadow-pass culling, jittered views, kernel binding)

* **Implemented**: opt-in `multimesh_gpu_culling/shadows` culls MultiMesh
  casters per shadow camera (near plane never culls); GPU culling and CLOD now
  run under TAA/upscaler jitter with a jitter-widened frustum (HZB stays off);
  cull kernel binds shared buffers once per mode; shadow counters in statistics.
* **Validated**: all large_world suites on Vulkan/D3D12 (statistics: exact spot
  shadow counts), dense forest with shadow culling and a TAA A/B (0 px), valley
  day/night, standard smoke.
* **Measured**: shadow culling costs 0.05-0.1 ms CPU without a clear GPU gain in
  the valley, so it is off by default (D011). Mistake recorded: positional
  shadows are cached, so static tests must dirty them.

## 2026-11-06 (per-instance MultiMesh LOD on the GPU)

* **Implemented**: the MultiMesh culling kernel picks an index LOD per visible
  instance (up to four levels between the CPU LOD of the bounds' nearest and
  farthest points); reduced-LOD surfaces are now culled too. Statistics report
  visible instances per level. Valley vegetation generates index LODs by default.
* **Validated**: new `multimesh_lod` fixture (exact per-level counts, coverage
  against per-instance CPU LOD, LOD-off pixel identity) and all large_world
  suites on Vulkan/D3D12, dense forest, valley day/night, standard smoke.
* **Measured**: large-range MultiMesh GPU 1.24 ms to 0.42 ms; valley GPU
  unchanged within noise, CPU +0.05-0.1 ms. A fixed-bucket first version
  regressed and was replaced (D010).

## 2026-11-06 (native GPU geometry statistics)

* **Implemented**: `RenderingServer.viewport_get_gpu_geometry_statistics`;
  MultiMesh culling, CLOD selection and HZB kernels add into a per-viewport
  buffer, cleared and read back asynchronously only while polled. Valley HUD and
  bench show the counts.
* **Validated**: new exact-count fixture (`statistics.tscn`) on Vulkan/D3D12,
  all large_world suites, dense-forest A/B, valley day/night and standard smoke.
* **Finding**: CLOD draws 17k–95k triangles in the valley while the frame
  submits 1.3–6.5 M primitives, mostly full-detail MultiMesh vegetation.

## 2026-11-06 (skip GPU culling for fully visible MultiMeshes)

* **Implemented**: MultiMeshes inside all frustum planes keep the direct draw and
  skip their three culling dispatches; never removes geometry.
* **Validated**: large_world suites and dense-forest A/B (new fully visible view:
  identical pixels, no culling dispatch) on Vulkan/D3D12. Valley bench: culling
  CPU overhead 0.1–0.18 ms lower where 112 of 172 patches were fully inside; no
  change in a view without such patches (noise about 0.1 ms).

## 2026-11-06 (CLOD multi-draw submission and valley benchmark)

* **Measured first**: new `devtools/test/forest_bench.ps1` (three fixed valley
  views, four modes, two alternating rounds of 240 frames). Resident CLOD added
  +0.17–0.31 ms render-thread CPU p50 over culling alone because it recorded one
  indirect call per group record.
* **Implemented**: internal `RD::SUPPORTS_MULTI_DRAW_INDIRECT` (Vulkan feature
  bit, D3D12 true, Metal false); CLOD submits each surface's records in one call,
  with the per-record loop as fallback.
* **Validated**: CLOD overhead now +0.02–0.06 ms on Vulkan and D3D12, GPU time
  unchanged; large_world suites, dense-forest pixel A/B, valley day/night and
  standard PhysX/PT/DDGI smoke pass. Also measured: GPU culling saves 0.17–0.36 ms
  GPU but costs up to 0.29 ms CPU; HZB costs 0.03–0.13 ms GPU and saves nothing
  in this scene.

## 2026-11-06 (visual valley smoke tests)

* **Added**: day and night valley fly-through scenes built from ordinary Godot
  nodes (720 m CLOD terrain, about 26k MultiMesh trees in 48 m patches, grass,
  flowers, 280 CLOD rocks, SSR water). Night adds volumetric fog, a lake
  FogVolume, 68 local lights, emissive props and vertex-animated fireflies that
  must use the conventional path. Also kept the dense-forest A/B pixel test.
* **Validated**: day/night on Vulkan and D3D12: all eight route views dispatch
  culling, CLOD and HZB; disabling GPU geometry dispatches none; night light and
  volumetric-fog negative controls differ. Dense-forest A/B passes on both.
* **Testing correction**: the first fly-through read timestamps after
  `get_image()`, which resets them; see the new mistake entry. One-run GPU times
  (enabled vs disabled, one view) differ by about 1%; no performance claim.

## 2026-11-05 (residency cut protocol)

* **Implemented**: CPU coverage snapshots and GPU missing-representation cuts,
  complete coarse fallback and shared requested/used feedback. No page pool or
  streaming claimed; native raster retains full residency.
* **Validated**: production build, Vulkan/D3D12 eight-part suites and standard
  smoke; 24 snapshots, two thresholds, two roots, cut coverage and guarded commands.

## 2026-11-05 (deferred editor help lifetime)

* **Investigated**: repeated editor --import shutdown failure; static help-worker
  callback can outlive EditorNode and enter script-cache work after teardown.
* **Patched**: guard editor lifetime at script-doc-cache entry; no automatic retry
  or broad singleton behavior change. Production build, six repeated imports
  (exit 0, no errors) and standard PhysX/PT/DDGI smoke pass. Other shutdown races
  are not claimed excluded without a crash stack.

## 2026-11-05 (current-frame CLOD HZB)

* **Implemented**: optional complete-depth-then-opaque occlusion, per-viewport
  padded reverse-Z minimum-depth pyramid, guarded native commands and conservative
  clip/coverage/float-uncertainty bounds. No temporal visibility or CPU readback.
* **Validated**: production build, final Vulkan/D3D12 seven-part matrices,
  exact mip oracle, actual command rejection and one-pixel-hole recovery; native
  material/shadow pixels, moving occluders, MSAA/depth fallback and lifetime.
  Numerical uncertainty and 100000-unit source/camera cancellation also pass;
  final standard PhysX/PT/DDGI smoke rerun passes. No performance improvement is claimed.
* **Testing correction**: shared mip-view readback used the wrong base subresource;
  owner-chain readback proves the actual GPU pyramid without a production fix.
* **Unresolved observation**: one editor --import shutdown hit null EditorNode;
  isolated retry, five repeated imports and subsequent complete suites pass.
  Root cause unknown, not hidden by automatic retries.

## 2026-11-05 (scene-import cook and optimized shadows)

* **Implemented**: default-off scene import cluster cooking after final source
  processing, before embedded/external save, once per shared mesh; unsupported
  geometry retains conventional data with warning.
* **Extended**: native CLOD camera depth uses source vertices even with separately
  indexed shadow meshes; shadows retain their own resource.
* **Validated**: production build, class-help checks, real enabled/disabled/morph
  glTF imports and Vulkan/D3D12 six-part matrices. Reversed shadow vertex IDs,
  source/fine pixels and coarse-cast-shadow receiver proof pass. Final standard
  PhysX/PT/DDGI smoke also passes.
* **Runner finding**: first 120-second outer budget expired; targeted import
  checks and complete 300-second-budget reruns pass, with no engine fix inferred.

## 2026-11-05 (native resident CLOD raster integration)

* **Implemented**: ArrayMesh attachment uploads checked
  resident group/index buffers through RenderingServer; per-viewport native
  selection produces source-index commands for existing depth/opaque materials.
* **Preserved**: source geometry/material RID, ordinary shadow/RT geometry,
  conventional fallback for unsupported views, deformation and budgets.
* **Limits**: 4096 groups/surface, 64 MiB/mesh resident index/group data;
  65536 groups and 4096 draws per viewport. No streaming or speedup claim.
* **Validated**: production build and Vulkan/D3D12 five-part matrices. Native
  raster fine/coarse, original materials, three views, transforms, source/RD
  mutation, recook, shadow pixels, fallback and destruction pass. Final standard
  PhysX/PT/DDGI smoke rerun also passes after multi-pass fallback hardening.

## 2026-11-05 (GPU hierarchy selection kernel)

* **Implemented**: source-index group commands selected by projected error and
  ancestor veto, equivalent to exclusive root cuts without a bounded local stack.
* **Validated**: real Vulkan/D3D12 compute versus independent root
  oracle, perspective/orthographic and frustum views, guarded five-word commands.
  Changing-input/recovery and repeated reverse submission pass on both backends.
  Production build and standard smoke pass. Not native raster integration.

## 2026-11-05 (ArrayMesh cluster attachment)

* **Implemented**: serialized optional cluster hierarchy, convenience generation,
  SHA-256 source-array matching, shared-resource change checks and conservative
  mutation invalidation. Materials, names and source RID remain authoritative.
* **Refactored**: same cache validator shared below scene/rendering boundary.
* **Validated**: production build, Vulkan/D3D12 tests of source mismatch, shared
  cache mutation, save/load, region updates and surface lifetime; focused XML
  checks and standard PhysX/PT/DDGI smoke pass. No GPU CLOD submission claimed.

## 2026-11-05 (stable GPU compaction)

* **Fixed**: unordered atomic append replaced by local ranks, block-prefix scan
  and stable scatter, preserving coplanar instance draw order and source identity.
* **Validated**: Vulkan/D3D12 ordered-ID kernel checks across 261 scan blocks,
  shared-view/resource A/B and 100k instances with coplanar colored IDs; standard
  PhysX/PT/DDGI smoke suite passes. No performance improvement claimed.

## 2026-11-05 (native cluster cooker)

* **Implemented**: MeshClusterData resource, module-backed meshlet/group cooker,
  checked cache, border-locked attribute-aware simplification and root-traversed
  reference cuts. Existing meshoptimizer 1.2, no new dependency or copied engine code.
* **Validated**: production build, headless and Vulkan/D3D12 fixture runs;
  curved-grid topology/cut coverage, source preservation, resource roundtrip,
  corrupt-data rejection and extreme-finite projection inputs.
* **Fixed during smoke**: self-closing tutorials section was accepted by RST but
  rejected by native editor help parser; explicit section closing added. Final
  standard PhysX/PT/DDGI smoke rerun passes.

## 2026-11-05 (signed instance-ID compatibility)

* **Fixed**: visible-ID remap must return shader-language `int`, not `uint`.
  Regression shader exercises signed arithmetic and an `int` function argument
  with both conventional and GPU-culled rendering. Production build and
  Vulkan/D3D12 focused regression matrices pass.

## 2026-11-05 (continuous implementation and commits)

* **Updated**: working rules: commit each tested part with its records, then
  continue the explicitly authorized implementation rather than stop per slice.

## 2026-11-05 (native GPU frustum culling)

* **Implemented**: experimental Forward+ MultiMesh compute visibility, stable
  source IDs, per-viewport buffers/indirect commands and conventional fallback.
* **Validated**: final production build; Vulkan/D3D12 native-kernel and native
  raster A/B checks at 65/100k potential instances, dual viewport dispatch,
  resize and shared-resource replacement. Standard PhysX/PT/DDGI smoke rerun passes.
* **Recorded**: explicit virtual geometry / CLOD / far-field voxel follow-on
  scope in `knowledge/VIRTUAL_GEOMETRY.md`; no placeholders called implemented.
* **Updated**: project concepts, source audit, architecture, status and validation;
  new-TU and typed-GDScript-fixture mistakes. Full docs check has unrelated
  existing PhysX errors; focused ProjectSettings docs check passes.

## 2026-11-05 (native large-world rendering foundation)

* **Added**: source audit, architecture, decisions, port status and validation
  runbooks in `knowledge/`; project/native-indirect-foundation.md links them.
* **Found**: indirect MultiMesh setter uploaded the CPU `-1` sentinel as an
  unsigned GPU count; real-RD reproducer checks both surface command layouts.
* **Validated**: production editor build, focused Vulkan/D3D12 command/pixel
  tests and standard PhysX/PT/DDGI smoke suite pass. No scalability/performance claim.
* **Added**: mistakes/python-build-interpreter.md and
  successes/inspect-indirect-before-draw.md.

## 2026-11-02 (release 0.8.1)

* **Updated**: release 0.8.1 (reflections, DDGI adaptive update budget)
  in CUSTOM_BUILD.md; README.md lists the reflection features.

## 2026-11-02 (mirror probes off by default)

* **Updated**: REFLECTIONS.md: `auto_mirror_probes` defaults to off.

## 2026-11-02 (planar distance limits, presets)

* **Updated**: REFLECTIONS.md settings (`planar_max_distance`,
  `planar_mirror_max_distance`), findings, performance plan P7.

## 2026-11-02 (mirrors first for the mirror pass)

* **Updated**: REFLECTIONS.md settings and findings: upright reflectors get
  the mirror pass before floors.

## 2026-11-02 (mirror pass shadow LODs)

* **Updated**: REFLECTIONS.md performance plan P4: shadow passes in the
  mirror pass use the view's whole projection for LODs; mirror occlusion
  culling deferred.

## 2026-11-02 (probe recaptures with a moving sun)

* **Updated**: REFLECTIONS.md performance plan P6: measured, bounded, no
  change needed.

## 2026-11-02 (sky flashing in the mirror)

* **Updated**: REFLECTIONS.md issue J and findings (fog volume created
  before the sky is set up; no reprojection on a new volume's first frame).
* **Added**: mistakes/metric-blind-to-the-artifact.md.

## 2026-11-02 (mirror pass LODs)

* **Updated**: REFLECTIONS.md performance plan: the mirror pass picks mesh
  LODs from the view's whole projection and its own resolution (P4).
* **Added**: mistakes/test-scene-without-hard-quit.md.

## 2026-11-01 (brightness against the path tracer)

* **Added**: mistakes/tonemapped-noise-averages-darker.md.
* **Updated**: REFLECTIONS.md issue I and findings; probe emissive-light limitation.

## 2026-11-01 (DDGI in probe captures, performance plan)

* **Updated**: REFLECTIONS.md: `probe_capture_ddgi`, step 9 done, issue H
  done, performance plan, findings; reflections/planar-open-issues.md.

## 2026-11-01 (reflections: bugs seen in play)

* **Added**: mistakes/motion-test-that-did-not-move.md.
* **Updated**: REFLECTIONS.md findings (mirror pass selection by screen
  coverage, edge halo, mirror pass jitter) and open issues H and I;
  reflections/planar-open-issues.md.

## 2026-11-01 (reflections: open issues)

* **Added**: mistakes/stale-generated-shader-header.md.
* **Updated**: REFLECTIONS.md open issues (A, B, F done; C physical; D
  and G status), findings; reflections/planar-open-issues.md.

## 2026-11-01 (glass research)

* **Added**: research/cheap-glass-shading.md.

## 2026-11-01 (mirror pass cost)

* **Updated**: REFLECTIONS.md findings and limitations: mirror pass cropped
  to the reflector, no post-processing; timestamps before and after.

## 2026-11-01 (mirror pass, step 8)

* **Updated**: REFLECTIONS.md: `planar_mirror_render`, `planar_mirror_scale`,
  findings (clip distances, light culler winding, DDGI sharing, hybrid
  resolve, cost); reflections/planar-open-issues.md.

## 2026-11-01 (reflection resolution, mirrored-camera research)

* **Added**: research/mirrored-camera-reflections.md (step 8 sources).
* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflection_resolution`.

## 2026-11-01 (mirror probes)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_mirror_probes` (a probe in
  front of each mirror, for what close mirrors reflect off screen),
  findings log; reflections/planar-open-issues.md.

## 2026-11-01 (planar quality, paused)

* **Added**: reflections/planar-open-issues.md; REFLECTIONS.md "Open issues"
  (A to G) with repro positions and movement test commands.
* **Updated**: REFLECTIONS.md findings: temporal filter, footprint fill,
  coverage, normal check, automatic scan bounds and filters.

## 2026-11-01 (automatic reflections)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflections` (volume probe and
  planar reflectors found from mesh planes and material roughness).

## 2026-10-31 (reflections)

* **Added**: REFLECTIONS.md (runbook) and reflections/: overview and the
  architecture decision (pixel-projected planar reflections, budgeted and
  DDGI-normalized reflection probes, atlas on demand).
* **Added**: research/pixel-projected-reflections.md,
  research/cubemap-normalization.md.
* **Added**: mistakes/powershell-single-arg-splat.md,
  successes/reflection-atlas-on-demand.md.
* **Updated**: ddgi/improvements/specular-occlusion.md: probe reflections
  are now normalized by DDGI.

## 2026-10-31 (sync)

* **Updated**: CUSTOM_BUILD.md sync log: upstream `95c0fa5fe8` (4.8-beta),
  physx module `97bd583c28`. Shader `CONTAINER_VERSION` 5 -> 6 (upstream
  added a container header). Smoke: PhysX GPU, path tracer and DDGI pass;
  the PhysX tests report one error only because the checkout's `bin/` has
  no Streamline DLLs.

## 2026-10-31 (near-field rays removed, glass rays)

* **Added**: ddgi/findings/near-field-camera-spots.md: near-field rays
  removed from the engine (dark spots moving with the camera).
* **Added**: ddgi/findings/glass-probe-rays.md: four bugs on DDGI rays at
  refractive surfaces, fixed.
* **Updated**: near-field proposal and success marked deprecated.

## 2026-10-12 (performance and stability pass)

* **Updated**: DDGI.md findings: baseline per pass, apply sub-pass
  timestamps, reflection buffer written once, upsample block loads, the
  dropped distance blend skip, the `--chaos` stability test.
* **Updated**: tooling/vulkan-sync-validation.md: a too old layer reports
  false SPIR-V errors.

## 2026-10-12 (dark places)

* **Updated**: DDGI.md step 30: no flicker or blotches in dark places;
  new `tunnel` test view.

## 2026-10-12 (moving objects of any shape)

* **Updated**: DDGI.md step 29: moving objects as oriented boxes with exact
  face form factors (0.7.1).

## 2026-10-12 (moving objects vs path tracer)

* **Updated**: DDGI.md step 28: moving objects' light matched against the
  path tracer.

## 2026-10-12 (moving objects, no noise)

* **Updated**: DDGI.md step 27: moving objects lit as spheres by one ray
  per block, no noise.

## 2026-10-12 (moving objects)

* **Updated**: DDGI.md step 26: probes skip moving objects, near-field
  history lets go faster near them (the smears behind a moving ball).

## 2026-10-12 (near-field rays)

* **Added**: [near-field rays](/ddgi/improvements/near-field-rays.md)
  (proposal 12, done), finding [probes put nearby bounce light in the
  wrong place](/ddgi/findings/near-field-blob.md), success
  [near-field rays](/successes/near-field-rays.md), mistakes
  [a test setting in the animation callback](/mistakes/test-setting-in-animation-callback.md)
  and [a long run of an unfit scene](/mistakes/long-run-of-an-unfit-scene.md).
* **Updated**: [test protocol](/tooling/ddgi-test-protocol.md) with the
  bleed view tests (still and animated).
* **Verified**: emissive surfaces as direct lights (previous entry): the
  storm level shows a smooth light pool from the moving sphere.

## 2026-10-12 (emissive lights, work in progress)

* **Added**: references [DDGI papers and reference code](/research/majercik-ddgi-reference.md)
  and [O3DE DiffuseProbeGrid](/research/o3de-diffuse-probe-grid.md).
* **In progress**: emissive surfaces as direct lights in the DDGI apply pass
  (`rendering/global_illumination/ddgi/emissive_lights`), against light
  spots, blocky glow and bleeding from small emitters. Not verified yet.

## 2026-10-12 (least-squares bake)

* **Implemented**: [least-squares baking](/ddgi/improvements/least-squares-bake.md)
  on `dev-ddgi-quality`; [success](/successes/least-squares-bake.md).
* **Reread** the six GI papers for these two features: SDFDDGI's min/max
  checkerboard went into the low resolution apply; Enemies' dilation
  caveat (it creates new leaks) kept probes inside geometry out of the
  solve and its smoothness term.

## 2026-10-12 (low resolution apply)

* **Implemented**: [low-resolution apply](/ddgi/improvements/low-res-apply-directional.md)
  on `dev-ddgi-quality`, default 1/4: DDGI 1.40 -> 1.13 ms at 1080p.
* **Added**: finding [full resolution pass floor](/ddgi/findings/full-res-pass-floor.md),
  mistake [encoding convention mismatch](/mistakes/encoding-convention-mismatch.md).

## 2026-10-12 (second research set)

* **Added**: 6 references (Unity 6 APV, tetrahedral interpolation, CoD:
  Infinite Warfare precomputed lighting, Stupid SH Tricks, GI downscaling,
  FF7 Rebirth GI) and an unread stub for a Zhihu article (HTTP 403).
* **Added**: proposals 9-11 (low-resolution apply with directional output,
  specular occlusion, least-squares baking) and two small ideas.

## 2026-10-12 (scheduler 2x, moving lights)

* **Corrected**: [favored probes and flicker](/ddgi/findings/scheduler-favored-set-stability.md):
  the cause was the 3x rate, not the window; shipped 2x.
  [Mistake recorded](/mistakes/three-runs-of-a-noisy-test.md).
* **Added**: [moving light trailing](/ddgi/findings/moving-light-trailing.md)
  (open issue, two rejected fixes), the emissive step and weather stepping
  tests in [the test protocol](/tooling/ddgi-test-protocol.md).
* **Updated**: sample-driven results in the proposal and the success page.

## 2026-10-12 (mistakes and successes)

* **Added**: [mistakes](/mistakes/) (7) and [successes](/successes/) (6)
  sections, the `Mistake` and `Success` types, and rule 8 in
  [the howto](/kb-howto.md): record both as they happen.

## 2026-10-12 (later)

* **Added**: [DDGI test protocol](/tooling/ddgi-test-protocol.md), from
  the measurements for sample-driven updates: step response with
  `--sun_step`, startup not reproducible, flicker spread over 3 runs,
  A/B with one editor copy per variant, and a storm game-level test with a
  moving emissive object (fixed exposure, `--fixed-fps 60`, averages
  only).
* **Added**: findings [extra code in the apply pass costs](/ddgi/findings/shader-code-cost-without-work.md)
  and [favored set stability](/ddgi/findings/scheduler-favored-set-stability.md).
* **Updated**: [sample-driven updates](/ddgi/improvements/sample-driven-updates.md)
  is implemented on `dev-ddgi-sched`, with results.

## 2026-10-12

* **Initialization**: Created the bundle: project overview, DDGI and XeGTAO
  state, the findings from the XeGTAO work (render graph ordering issue,
  convergence measuring), six GI papers and talks with what they mean for
  DDGI, and a ranked list of DDGI improvement proposals.
