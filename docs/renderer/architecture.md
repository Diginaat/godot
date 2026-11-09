# Forward+ renderer architecture (Phase 0 audit)

How a 3D frame goes from scene changes to GPU submission in this fork, what
runs on which thread, and where the CPU and GPU work is. Audited on
`dev-gpu-renderer` at `4bc2a22774` (Godot master `b47a795aa5` + NVIDIA + PhysX
module + fork work), 2026-11-10. Line numbers are from that commit and drift;
search for the symbol when they do.

Abbreviations: SC = `servers/rendering/renderer_scene_cull.cpp`, RFC =
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`,
RD = `servers/rendering/rendering_device.cpp`, RDG =
`servers/rendering/rendering_device_graph.cpp`, RSD =
`servers/rendering/rendering_server_default.cpp`.

## Frame pipeline

```text
main thread                         render thread (or main, thread_model=safe)
-----------                         ------------------------------------------
Node3D transforms -> RS setters     RSD::_draw()
 (FUNC2: queued, async)              |- scene->update()            [frame_setup_time]
RenderingServer::sync()              |    BVH optimize_incremental, dirty instances,
 (main.cpp: waits for previous       |    re-pairing, update_dirty_resources
  frame's _draw)                     |- particles, render_probes
                                     |- viewport->draw_viewports()  [viewport CPU time]
                                     |    render_camera()  (SC)
                                     |      _render_scene: cull (threaded scan)
                                     |      directional cascades (in the same scan)
                                     |      omni/spot shadow cull (serial, BVH)
                                     |    RenderForwardClustered::_render_scene (RFC)
                                     |      _fill_render_list, sort, _fill_instance_data
                                     |      GPU culling/CLOD/HZB compute (fork)
                                     |      shadows, GI, depth, opaque, sky, alpha, post
                                     |      draw lists recorded on one thread
                                     |- canvas, blit, end_frame -> RDG sort + submit
```

`thread_model=separate` (`--render-thread separate`) moves the right column to
a render thread. Setters are queued; getters (`FUNC*R`) block the caller
(`servers/server_wrap_mt_common.h`).

## Scene database (CPU)

- `Instance` (SC.h:465) is a large AoS object per instance in an `RID_Owner`.
- Hot data is split out per scenario: `instance_aabbs` (bounds) and
  `instance_data` (flags, layer mask, base RID, geometry pointer) as two
  parallel `PagedArray`s (SC.h:415). Insert appends, remove swaps with last.
- `DynamicBVH` per scenario (geometry, volumes) is used for pairing and for
  omni/spot shadow queries, **not** for the main camera cull.
- Changes go through `_instance_queue_update` (SC:654) into an intrusive dirty
  list, drained every frame in `update_dirty_instances`. A moved instance
  updates its BVH leaf and **re-pairs** with lights/probes/GI (SC:2015-2063).
  A moving shadow caster dirties every paired light's shadow (SC:1863).

## Culling (CPU)

- Main cull (SC:4097-4135): brute-force linear scan of `instance_data`,
  split into equal contiguous ranges over all worker threads above
  `rendering/limits/spatial_indexer/threaded_cull_minimum_instances`
  (default 1000). No load balancing.
- Per instance: visibility flags, layer mask, sign-indexed AABB/plane test,
  visibility range, CPU occlusion (HZB from Embree raster, optional), then
  sorting into `InstanceCullResult` lists. The same loop also tests every
  instance against each directional shadow cascade, collects RT/TLAS AABBs
  (fork, path tracer/DDGI) and SDFGI regions.
- SpinLocks inside the hot loop (probes, VoxelGI, lightmaps) and one
  `call_on_render_thread` per visible particle system per frame.
- Mirrors (fork `PlanarReflector3D`) and reflection-probe faces run a full
  second `_render_scene`, cull included.
- Omni/spot shadows: `_light_instance_update_shadow` (SC:2613) per visible
  shadowed light, serial, BVH `convex_query` per face (6 for cube). Only
  dirty shadows are re-culled and re-drawn.
- Fork: far directional cascades are reused between frames
  (`directional_shadow/far_cascade_update_interval`, see
  [kb/project/directional-cascade-cache.md](../../kb/project/directional-cascade-cache.md)).

## Render list and submission (CPU, one thread)

- `_fill_render_list` (RFC:1042): every visible instance, every surface;
  computes depth layer, flags, CPU mesh LOD (`mesh_surface_get_lod`), GI ids
  (linear scans). Repeated for every shadow pass (cascade, light face).
- Sort: `sort_by_key` (opaque, motion), reverse depth (alpha), per shadow
  range (RFC:2415, 3378). Key: priority, shader, material, geometry, surface,
  depth layer, LOD (RFC.h:779).
- `_fill_instance_data` (RFC:913): rewrites a 176-byte `InstanceData` per
  render element **every frame** into a persistently mapped ring buffer
  (`MultiUmaBuffer`), one flush. No persistence across frames, no dirty ranges.
- Merging: consecutive elements with equal keys become one instanced draw
  (`repeat`). Skinned, blend-shape and MultiMesh never merge. Depth layer and
  LOD in the key split otherwise equal draws.
- `_render_list_template` (RFC:349): per element, pipeline key rebuild/hash,
  vertex-format lookup, redundant-bind filtering, push constant, draw.
- RD can record secondary command buffers on workers (RDG:1098), but
  `SECONDARY_COMMAND_BUFFERS_PER_FRAME` is 0 (RD:159, "IHV issues").
  `draw_list_begin_split` is deprecated. All recording is on the render thread.

## Fork GPU-driven paths (already built)

All opt-in, Forward+ only, main view (MultiMesh shadow culling separately).
Details and evidence in [knowledge/](../../knowledge/PORTING_STATUS.md).

| Path | Files | Scope |
| --- | --- | --- |
| MultiMesh GPU frustum culling + per-instance LOD + distance thinning | `forward_clustered/multimesh_culling.cpp`, `multimesh_cull.glsl` | Per surface: cull, scan, scatter dispatches; source-ID remap in the scene shader; stable order |
| Cluster LOD (CLOD) selection and raster | `forward_clustered/mesh_clod.cpp`, `mesh_clod_select.glsl` | Static `MeshInstance3D` with cooked `MeshClusterData`; multi-draw indirect |
| HZB occlusion (CLOD groups, MultiMesh instances) | `forward_clustered/mesh_hiz.cpp` | Current-frame pyramid after the depth pre-pass; two-pass for CLOD |
| CLOD paging from memory/disk | RD `mesh_storage.cpp`, `MeshClusterData` | Budgeted page pool, async feedback |
| GPU voxel far field, GPU grass | `VoxelField3D`, `GrassField3D` | Compute-filled indirect MultiMesh |
| GPU geometry statistics | `forward_clustered/geometry_statistics.cpp` | Async counters while polled |

The gap: ordinary `MeshInstance3D` nodes (the common case) still take the
whole CPU path above. GPU culling exists only where one CPU instance stands
for many GPU instances (MultiMesh) or one mesh for many clusters (CLOD).

## Lighting, shadows, GI

- Lights: CPU `update_light_buffers` (`storage_rd/light_storage.cpp:729`)
  sorts by distance, packs `LightData`, full buffer upload every frame.
  Binning is on the GPU: `ClusterBuilderRD::bake_cluster` rasterizes light
  volumes into the cluster buffer and packs with compute. Cap
  `rendering/limits/cluster_builder/max_clustered_elements` (512 per type;
  extra lights are dropped silently).
- Shadows (RFC:1835-1913): omni cube lights first (6 passes + 2 copies
  each), then one begin/append/process/end batch for directional and
  positional passes. Each pass builds its own render list on the CPU and
  updates two UBOs. Fork: shadow passes scale the LOD threshold
  ([kb/project/shadow-lod-scale.md](../../kb/project/shadow-lod-scale.md)).
- GI: SDFGI, VoxelGI and the fork's DDGI (path tracer subclass only) run on
  the single queue in recorded order. "(Parallel)" timestamps mean graph
  reordering, not concurrent queues. DDGI rebuilds its TLAS on the CPU every
  frame (`render_raytracing.cpp`).

## RenderingDevice

- 2 frames in flight by default (`vsync/frame_queue_size`), one fence each.
- Render graph (RDG): one node per draw list or compute list; automatic
  barriers from resource trackers; reorder by level and type. A timestamp
  node depends on everything since the previous one, so timestamps limit
  reordering (see AGENT_MEMORY "Never capture an RD timestamp per object").
- Uploads: `buffer_update` goes through 256 KiB staging blocks (up to
  128 MiB). Running out stalls all frames. `DYNAMIC_PERSISTENT` buffers bypass
  staging (persistently mapped ring).
- `buffer_get_data` stalls the GPU; `buffer_get_data_async` returns about
  two frames later without a stall.
- Recording is render-thread only (`ERR_RENDER_THREAD_GUARD`). Resource
  creation works off the render thread (transfer workers).
- Pipelines compile on WorkerThreadPool with ubershader fallback; disk
  pipeline cache on.
- One queue in use for graphics and compute. A transfer queue is only used
  for initial resource data.

Capability matrix: [compatibility.md](compatibility.md).

## Viewport and 2D

`can_draw_2d` (`renderer_viewport.cpp:372`) is true for every single-view
viewport with 2D enabled, so a 3D-only window still runs the canvas block
(the root World2D canvas always exists). With no items the cost looks small;
not measured yet (Phase 10).

## Profiling hooks

- `viewport_set_measure_render_time`: viewport CPU (includes cull and render
  list) and GPU time. `get_frame_setup_time_cpu`: `scene->update`.
- `RenderingServer.set_frame_profiling_enabled` + `get_frame_profile`: CPU
  and GPU time per `RENDER_TIMESTAMP` marker ("Cull 3D Scene", "Render 3D
  Scene", "Render Opaque Pass", ...). Markers add graph nodes; profile
  separately from timing.
- `viewport_get_render_info`: objects, draw calls, primitives (visible and
  shadow). Note: CLOD group records count as draw calls.
- `viewport_get_gpu_geometry_statistics` (fork): GPU culling/CLOD/HZB counters.
- Tracy-style `GodotProfileZone` zones in RSD, viewport and main loop.
- `get_memory_usage`, `get_driver_and_device_memory_report` (Vulkan).
- Missing: a counter for culled instances and cull time per view.
