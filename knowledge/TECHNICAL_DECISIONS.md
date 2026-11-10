# Technical decisions

## D001 — Extend inherited Forward+, never embed another engine

2026-11-05. Accepted architectural constraint. The selected PT renderer inherits
normal raster rendering. Use existing MeshStorage, pass lists and RD resource
ownership. NVRHI and a second scene/resource layer would duplicate Godot and
break native materials. Preserve path tracing and PhysX.

## D002 — Compute plus indexed indirect before mesh shaders

2026-11-05. Initial design. Existing MultiMesh commands already reach ordinary
surface raster. A reference engine's mesh-shader result is not a Godot benchmark and its
startup feature floor excludes otherwise supported devices. No new vendor API
or mesh-shader dependency for the baseline. Reconsider only after equivalent
Godot measurements prove a useful capability/cost tradeoff.

## D003 — Stable source IDs and pass-scoped output

2026-11-05. Required future invariant. Compact ID lists, not the source transform
buffer. Packing transforms changes instance identity/history, while one shared
culled command cannot simultaneously describe main, shadow and reflection views.
No automatic culling until both identity and submitted lifetime are addressed.

## D004 — Correct existing command semantics first

2026-11-05. First practical slice. A reference engine bounds work-list counts explicitly;
Godot's indirect setter currently uploads `-1` as `0xffffffff`. Normalize the
CPU sentinel to allocated capacity at the CPU/GPU boundary, preserving public
`-1` semantics and initial zero commands. Prove actual command bytes and bounded
GPU writes before using this infrastructure at large counts. No claim that this
small fix ports a complete GPU geometry pipeline.

## D005 — No unlicensed source transplant or speculative voxel renderer

2026-11-05. No engine-wide license found for the reference code. Do not copy its implementation
text or import new dependencies; independent Godot work can proceed. Voxel far
field is experimental, material-incompatible and tied to atomic visibility.
Defer it rather than replace Godot's renderer to support a prototype feature.

2026-11-06, superseded in part: the owner of the reference code also owns
this build and has cleared the code for use here, so it may be ported
directly. The architectural half still holds: Godot's
material renderer stays, and no atomic visibility buffer replaces it.

## D006 — One canonical OKF bundle

2026-11-05. The requested `knowledge/` documents are project runbooks/evidence.
`kb/` remains this fork's OKF bundle and links to them; do not create a second
competing OKF bundle or expose local source checkout paths in shared memory.

## D007 — First culler uses a project opt-in, not a new resource ABI

2026-11-05. Implemented. Add one experimental project setting, default off,
covering eligible conventional MultiMeshes. Do not reinterpret existing
`use_indirect` resources or GPU-written instance counts. No new RenderingServer
method or MultiMesh serialization change is needed. Culling emits per-surface
commands and source IDs into per-viewport RD buffers; specialized ordinary
pipelines compile out the ID indirection. Per-resource policy and shared
multi-surface visibility are deferred until measurements justify them.

## D008 — CLOD/virtual geometry and voxels are explicit follow-on deliverables

2026-11-05. User confirmed scope. Proceed from tested instance visibility to
attribute-preserving cluster cooking, resident CLOD raster, conservative HZB,
then virtual page residency/streaming and coverage-correct distant voxels. Keep
the native Godot material/lighting path. Do not import Unreal/Nanite code or
a reference engine's atomic visibility renderer. See VIRTUAL_GEOMETRY.md for acceptance gates.

## D009 — Multi-draw CLOD submission behind an RD capability

2026-11-06. Implemented and measured. Resident CLOD recorded one RD indirect
call per group record, including the many zero-instance records, which made
CLOD's render-thread cost grow with total group count. Add an internal (not
script-bound) `RD::SUPPORTS_MULTI_DRAW_INDIRECT` capability and submit a
surface's contiguous records in one call when present. Keep the per-record loop
as the fallback, so no backend loses CLOD. No GPU-count compaction yet: GPU time
did not change, and an indirect-count path needs its own capability check.

## D010 — Per-instance MultiMesh LOD inside the culling kernel

2026-11-06. Implemented and measured. With index LODs on valley vegetation,
Godot's per-MultiMesh LOD removed reduced-LOD patches from GPU culling, and a
large MultiMesh draws all instances at the detail of its nearest one. Choose
the level per visible instance in the existing cull/scan/scatter kernel instead
of adding a pass: up to four levels with their own ID ranges and indirect
records. Level 0 stays the CPU-selected LOD and the last level the CPU LOD of
the farthest bound point, so results are never coarser than Godot's rule.
Rejected: fixed buckets over all LODs (0, L/3, 2L/3, L). Measured on the valley,
it drew LOD 1-2 instances at LOD 0 and lost the fully-inside skip: view 1
culling GPU 7.23 ms versus source 6.94 ms. Four levels keep the packed-byte
scan; more levels need a wider counter.

## D011 — Shadow-pass MultiMesh culling is a separate opt-in

2026-11-06. Implemented and measured. Shadow passes reuse the culling kernel
against each shadow camera, behind `rendering/geometry/multimesh_gpu_culling/shadows`
(default off). In the valley the cascades see most casters (view 1: 17,366 of
19,465 tested casters visible), so the 133-233 extra dispatches cost 0.05-0.1 ms
of render-thread time without a GPU gain that stands out of run-to-run noise.
Scenes where shadow cameras see a small part of large MultiMeshes (local spot
lights, tight cascades) are where it should pay; that case is not measured yet.
Jittered views were enabled at the same time with a widened frustum instead of
a separate setting: it is exact and adds no work.

## D012 — CLOD paging starts from memory, one page per cluster group

2026-11-06. Implemented and tested; not benchmarked. Streaming pages are whole
cluster groups because the residency-aware cut already decides per group, so a
page needs no extra metadata and an evicted group falls back to its resident
parent. All streamed surfaces share one GPU index pool sized by
`cluster_lod/streaming/budget_mb`; roots are pinned, so every surface whose
roots fit always has a complete cut. Pages come from the cooked indices held in
CPU memory. Disk-backed loading needs a worker, a file format for pages and
resource generations; it is the next step, not a reason to block the GPU side.
Feedback is read back asynchronously and dropped when the request layout
changed, which trades a few frames of latency for never waiting on the GPU.
Default off: it only saves GPU memory, and the fixture shows exact convergence
with an ample pool but no frame-time measurement yet.

## D013 — Far-field proxies are CPU-built voxel splats, not a GPU voxel traversal

2026-11-06. Implemented and measured. `VoxelProxyBuilder` turns meshes and
MultiMeshes into one static splat mesh: area-weighted samples merged per voxel,
one camera-facing quad per voxel sized by its coverage. A static mesh needs no
new renderer path: it uses Godot's materials, shadows, culling and every
backend, and it can be saved as a resource. Earlier reference work showed that splat
coverage and flicker, not raw speed, are the hard part, so the builder keeps
coverage measurable (fixture: grove coverage within 25%, colour within 0.08).
Rejected for now: a GPU sparse voxel traversal, which would need its own
raster, shadow and material path. Measured in the valley: far patches beyond
180 m as proxies save 0.23-1.0 ms GPU (7-16%) on Vulkan and D3D12. The switch
is a hard distance cut with a 4 m overlap; dithered transitions and a far-field
image metric are open.

## D014 — Distance thinning is a per-MultiMesh property applied in the culling kernel

2026-11-06. Implemented and measured. Dense ground cover gets
`MultiMesh.distance_thinning_begin/end`: between the two distances a linearly
falling fraction of instances is kept, chosen by a fixed hash of the instance
index so nothing flickers while the camera moves. The test runs in the existing
GPU culling kernel (no extra pass); other renderers ignore the property, which
keeps the resource portable. Distances are converted to local units with the
node's smallest scale, so at least the requested fraction survives a
non-uniform scale. Thinned surfaces lose the fully-inside skip, because every
instance needs its distance test.
Measured on the Vulkan valley (ground cover thinned at 30-220 m): drawn
instances fall 13-23% per view, GPU p50 is unchanged within run-to-run noise
(view 1: 6.24 ms with thinning, 6.27 ms without, culling mode). The valley is
not bound by ground-cover vertices, so thinning is off by default in the bench
and left to scenes where it pays.

## D015 — GPU grass fills an indirect MultiMesh from a compute pass

2026-11-06. Implemented and measured. A reference engine's grass regenerates blades in
a mesh shader and in its visibility-buffer resolve; Godot's RenderingDevice
has neither mesh shaders nor a visibility buffer. `GrassField3D` keeps its
core property (blades are a pure function of patch, index and seed, nothing
is stored) but writes the visible blades of each frame into an indirect
MultiMesh with a compute pass and lets Godot's existing indirect draw render
them. That needs no change to the renderer, works with every material, light
and fog feature, and runs on Vulkan and D3D12 alike. The cost is 64 bytes per
blade of buffer capacity and one transform write per blade per frame.
Rejected: CPU-placed blade MultiMeshes (memory grows with the area, see the reference
estimate) and a new renderer path (more code for the same blades).
Measured in the valley (45 m radius, 72-93k blades): +0.21 to +0.45 ms GPU p50
per view on Vulkan and D3D12.

## D016 — Far-field switch uses Godot's visibility-range fade

2026-11-06. Implemented and measured. The 180 m switch between vegetation
patches and voxel proxies now cross-fades over 8 m with
`VISIBILITY_RANGE_FADE_SELF` on both sides instead of a hard cut with an
opaque overlap. Godot derives both alphas from the same distance rule
(smoothstep alphas that sum to one) and moves only the fading instances to the
alpha pass. A per-pixel dither would have needed a shader change in every
vegetation material and, because Godot's `MODEL_MATRIX` for MultiMesh
includes the instance transform, could not fade a whole patch consistently.
Measured: cross-fade and hard cut differ by at most 0.04 ms GPU p50 per view
in two alternating runs, which is inside run-to-run noise. The transition is
checked visually only; there is no transition image metric yet.

## D017 — CLOD pages from disk: one file per resource, read by worker batches

2026-11-06. Implemented and tested; not benchmarked. `MeshClusterData.save_pages`
writes a `.clodpages` file: a header with a SHA-256 of each surface's indices,
groups and clusters, then the raw indices in group order. The surface records
keep the path, the hash and the surface entry, so a renderer opens the file
only when the hash matches; a missing or mismatched file is reported once and
the mesh streams from memory. With a page file the renderer keeps only the
roots' indices (pinned anyway, and needed to rebuild the pool) and reads every
other group from disk on request: one worker task per file and frame, at most
1M indices in flight, consumed on the render thread once the task has
completed, so no lock is needed. Every setup gets a new id; reads for an id
that was released or set up again are dropped, which is the resource
generation check. Read indices are checked against the vertex count before
upload, because the hash covers only the header; a group that fails is not
read again until the next setup, and its ancestors draw. The resource itself
still holds every index (validation and the CPU reference cut need them), so
this halves the CPU copies rather than removing them; storing only the roots
in the resource and writing the file from scene import are the next steps.

## D018 — GPU voxel levels fill an indirect MultiMesh, one level per brick

2026-11-06. Implemented and measured. D013's static proxies have one voxel
size, so near the switch distance they read as blobs about 2 m wide.
`VoxelProxyBuilder.build_field` now ports a reference engine's voxel levels: the finest
cells are merged 2x2x2 into coarser levels (area summed, centroid, colour
and normal area-weighted), and the field is split into bricks that list
their voxels per level. `VoxelField3D` runs one compute workgroup per brick
each frame. The workgroup picks the coarsest level whose voxels are at most
`voxel_pixels` wide at the brick's nearest point (the reference per-level
selection), culls the splats against the frustum, and writes the visible
ones into an indirect MultiMesh, as in D015. Because each brick picks
exactly one level, there are no gaps or double coverage between levels.
The reference engine's atomic visibility buffer was not ported: Godot draws the splats with
its usual materials, fog and depth, on Vulkan and D3D12.
Choices:
* A voxel takes 16 bytes: brick cell plus centroid offset in 1/16 voxel,
  sRGB colour, octahedral normal and half size.
* The instance buffer grows with the count the GPU asks for, up to the
  data's largest selection. A fixed 262,144-splat buffer per field would have
  cost about 3 GB for the valley's 149 fields.
* Outside its visibility range a field selects nothing and skips its
  dispatch.
* Splats are round discs with the area of the square, both for this field
  and for the static proxies.
The valley flies with 64 voxels per patch edge (0.75 m finest) at 2 px.
128 voxels at 1.5 px was rejected: 13.2 M voxels for a 0.26 point lower image
error and 2.9 ms more GPU time.
Measured (Vulkan, culling mode, two alternating runs each): against the
static proxies, -0.05, +0.71 and +0.16 ms GPU p50 in views 0-2. Image error
against the source meshes at 27 m height: 3.69 against 5.13 (mean linear
difference x 1000), and 2.51% against 3.47% of pixels off by more than 0.05.
Shadows are off by default, because splats outside the camera view are not
selected.
Correction (2026-11-08): those GPU numbers missed the selection, which runs
before the viewport's timing starts. Whole frame, the GPU field cost 4.9 ms
more than the static proxies, 3.1 ms of it from one GPU timestamp per field
serializing the render graph. Without those timestamps it costs 2.0 ms more
(Vulkan; 1.6 ms on D3D12), mostly drawing ~27,000 instanced quads
(PERFORMANCE_BENCHMARKS.md).

## D019 — Scene import writes page files into the imported folder

2026-11-06. Implemented and tested. The import option
`meshes/cluster_lod_page_files` (default off, shown only with
`meshes/generate_cluster_lods`) calls `save_pages` on each cooked mesh before
the mesh is saved, so the saved cluster data records the file. The files go next
to the imported scene, `<imported scene>-<n>.clodpages` in `.godot/imported`,
also for meshes saved to their own resource file, and are returned as generated
files. The editor then lists them in the `.import` file's `files` and
`dest_files`, which is what export copies, and a reimport rewrites them.
A file that cannot be written is a warning; the mesh then streams from memory.
Reference: CRYENGINE 5.7's resource compiler splits a static mesh into a
header `.cgf` and a stream-only `.cgfm` holding the geometry, loaded through its
stream engine on demand; a missing stream file leaves the object unloaded rather
than failing. This is the same split, with Godot's checked resource as the
header. Ideas only: the CRYENGINE source is under its own licence, so no code is
taken from it.
Limits: files from an earlier import with more meshes are not removed (they are
not listed, so they are not exported); the resource still holds every index.
`import_paged` fixture (48x48 grid, Vulkan and D3D12): the path, the page magic
and both `.import` lists are checked; with streaming on, 72 disk reads give the
resident image exactly with 13,824 of 96,768 index bytes held.

## D020 — Paged-out cluster indices: the resource keeps only the roots

2026-11-06. Implemented and tested. `MeshClusterData.save_pages(path, false)`
writes the page file as before, then replaces each surface's `indices` with
its root groups' indices, concatenated in group order, and records the full
count as `index_count`. Cluster ranges keep addressing the full layout, so
selection, residency and the GPU group table are unchanged. Validation checks
cluster ranges against `index_count`, requires a page file, and requires the
held indices to be exactly the roots' ranges; a paged-out record cannot be
paged again (`ERR_UNAVAILABLE`) or have its page file cleared, because the file
is then the only copy. Scene import with `meshes/cluster_lod_page_files` now
uses it (D019).
Renderer: with streaming, setup copies the held roots and reads every other
group from the file as in D017. Without streaming, or when the surface cannot
stream, setup reads the whole surface from the file once (header and hash
check, every index checked against the vertex count). A paged-out surface
whose file is missing, damaged or stale is reported once and the whole mesh
draws without cluster LOD, the same fallback as any other unsupported mesh.
This follows CRYENGINE 5.7's stream-only geometry file: when it cannot be
opened, the object stays unloaded instead of failing (ideas only).
`import_paged` fixture, Vulkan and D3D12: the imported mesh holds 13,824 of
96,768 index bytes; tampered and page-file-less records are rejected; a
paged-out record round-trips; streamed and file-read resident images are
equal; a missing file, with streaming on and off, draws conventionally with
the mesh covered. All 16 large_world fixtures pass on both drivers.

## D021 — The project manager makes a backup before this build opens a project

2026-11-06. Implemented; builds; not clicked through in the GUI yet. This
build changes Godot's renderer, so a project opened in it may no longer work
in official Godot. Opening a single project from the project manager first
checks for `.godot/nvidia_dlss_physx_backup.cfg`. Without it, a "Backup
Required" dialog explains why, shows the project's size (everything the copy
takes, `.godot` included) and the free space on its drive, and warns when the
drive is too small. The only choices are "Back Up and Open" and "Cancel".
"Back Up and Open" runs upstream's duplicate dialog (pick a folder, copy);
when the copy succeeds, the original is marked with the backup path and date
and opens through the usual checks. The copy is not marked, so this build asks
again before opening it. Projects created with this build are marked at
creation. Opening several projects at once is refused while one of them is
unmarked. Limits: deleting `.godot` asks again; starting the editor directly
(`--editor --path`, used by the test scripts) does not ask.

## D022 — GrassField3D level of detail rings by complementary dithering

2026-11-06. Implemented and tested. Dense grass made of 500-vertex clumps out
to 24 m cost about 5 ms GPU at 1080p, mostly sub-pixel triangles drawn in the
depth pre-pass and the colour pass. `GrassField3D` now has `inner_radius`,
`inner_fade` and `edge_dither`. With edge dithering, each blade gets a hash in
[0, 1) from its patch, index and seed; the outer fade band keeps a blade while
the hash is at least the band's progress, the inner band while it is below.
Fields with the same seed, patch size, blade count and grids place identical
blades, so a near field with a detailed mesh and a ring field with a card mesh
whose inner band matches the near field's outer band draw every blade exactly
once. Patches are rejected conservatively in dither mode and blades decide
individually. No second instance or buffer per field; a ring is just another
field. Measured in a grass test scene at 1080p: 10.2 to 7.6 ms GPU (colour
pass 3.4 to 1.9 ms, depth pre-pass 2.0 to 1.3 ms) with detailed clumps to
8 m and the clumps' own billboard cards from 5 to 24 m. `grass_field` fixture
(Vulkan, D3D12): near 2551 + ring 3447 = 5998 blades, the same origins as one
field.
Noted, not changed: every `GrassField3D` sets a new custom AABB each frame,
which re-sends the instance transform and marks it moved, so its surfaces are
drawn in the motion-vector pass. Their motion vectors are still correct
(previous and current transforms are equal).

## D023 — Two-pass CLOD HZB occlusion with a per-viewport group history

2026-11-06. Implemented and tested; not benchmarked yet. Ports the two-pass
scheme of a reference engine's culling to the native CLOD path, which until now built
its HZB from the complete depth pre-pass and saved only colour work (the current-frame
baseline). With `rendering/geometry/cluster_lod/occlusion_two_pass` (default
on, used only with `occlusion_enabled`):
1. Before the pre-pass, a split kernel copies the selection commands into
   phase-1 depth commands, leaving out selected groups whose history word says
   they were occluded when last tested.
2. The HZB is built from the phase-1 depth and every selected group is tested:
   occluded groups lose their colour command and get history 1; visible ones
   get history 0, and those that were left out are written to phase-2 depth
   commands.
3. Phase 2 draws the opaque render list again in depth mode with only the
   CLOD elements (`mesh_clod_only`, so instance data indices stay), before
   SSAO, GI or anything else reads depth. The colour pass uses the tested
   commands.
History is one word per group per viewport, in an append-only arena keyed by
surface cache; a full arena drops every slot and starts cleared (all visible).
History is only a scheduling hint: a stale or wrong word costs a phase-2 draw,
never a missing group, because every left-out group is tested against the
phase-1 depth and drawn if not proven occluded. So no camera-cut detection is
needed. The occlusion test now runs right after the pre-pass instead of after
the pre-opaque effects. Shadows keep source geometry, so they are unaffected.
Same conditions as before: no MSAA, compositor, render region,
fragment depth or stencil writers. Jitter (TAA, FSR2, DLSS) was excluded
too, on the belief that the test used the unjittered projection; it does
not (`get_cam_projection()` includes the jitter), so the guard was removed
(same day; `hzb_two_pass` case D: TAA and FSR2 at half resolution give
images equal to occlusion off, and the forest TAA A/B is 0 px).
Statistics: `hzb_groups_deferred`, `hzb_triangles_deferred`,
`hzb_groups_recovered`, `hzb_triangles_recovered`.
Evidence: `hzb` kernel fixture checks split, recovery and history over four
rounds exactly; `hzb_two_pass` fixture (Vulkan, D3D12): a static view behind a
wall defers 15 of 15 occluded groups from phase-1 depth with an image equal to
occlusion off; 40 frames alternating between that view and a view that sees
the spheres equal the reference every frame, with groups recovered; single
pass defers none.

## D024 — MultiMesh instance HZB occlusion for the colour passes

2026-11-07. Implemented, tested and benchmarked. Opt-in
`rendering/geometry/multimesh_gpu_culling/occlusion` (default off). Until now
the HZB rejected only CLOD groups; GPU-culled MultiMesh instances were drawn
whenever they were inside the frustum.
1. The HZB (padded reverse-Z farthest-depth pyramid) is now shared:
   `_setup_hzb()` before the depth pre-pass, `_build_hzb()` once after phase 1,
   then the CLOD test and the MultiMesh test read it. Either feature alone
   builds it.
2. A new mode of `multimesh_cull.glsl` (`MODE_OCCLUDE`) reads the frustum
   pass's rank of each instance (visibility and LOD level, so depth and colour
   use the same level), tests the instance box in MultiMesh-local space with
   the shared `hzb_box_occluded()` (`mesh_hiz_inc.glsl`), and re-ranks. The
   existing scan (without statistics) and scatter modes compact the result into
   a second ID and command buffer.
3. The opaque and motion colour passes use the second buffers; the depth
   pre-pass and shadows keep the frustum lists. Phase-1 depth is a subset of
   the final depth, so anything behind it stays hidden.
4. With the setting on, MultiMeshes fully inside the frustum are dispatched
   too (otherwise their direct draw skips the test).
Rounding bound per instance: `abs(P) * abs(V) * abs(M)` (CPU, double) times
the box's local magnitude, times 128 float epsilons; non-finite or uncertain
boxes stay visible. Same conditions as CLOD occlusion (no MSAA, compositor,
render region, fragment depth or stencil writers); jitter is fine.
The command words are uploaded from the CPU, not copied on the GPU: on D3D12
a `buffer_copy` followed by compute writes to the destination fails the
command list (kb mistake `d3d12-buffer-copy-then-compute-write`).
Statistic: `hzb_instances_occluded`.
Evidence: `multimesh_occlusion` fixture (Vulkan, D3D12): two views and 30
alternating frames equal occlusion off exactly (636 and 46 instances
occluded; LOD spheres on a rotated, scaled node over several LOD levels), a
hidden MultiMesh fully inside the frustum has 16 of 16 occluded, TAA equal,
none with the setting off, negative control on visible rows. City bench:
opaque pass -0.37 to -0.59 ms for a 0.02-0.03 ms test (PERFORMANCE_BENCHMARKS).
Not done: depth-pass savings for instances (a history like D023); moving
(motion-list) MultiMeshes use the same path but have no fixture yet.

## D025 — GPU-first renderer: a persistent GPU instance table comes first

2026-11-10. Planned (Phase 0 of [docs/renderer/](../docs/renderer/roadmap.md)).
Problem: GPU culling, LOD and HZB exist for MultiMesh instances and CLOD
groups, but ordinary `MeshInstance3D` nodes still take the whole CPU path:
a threaded linear cull (cheap per instance), then per pass on one thread
`_fill_render_list`, sort, `_fill_instance_data` (176 bytes per element per
pass per frame, nothing kept between frames) and draw recording. Shadow
passes repeat all of it.
Candidates:
1. GPU culling of ordinary instances right away. Rejected as the first
   step: the GPU has no persistent scene data to cull, and returning
   visibility to the CPU would need a readback.
2. A separate deferred, bindless, fully GPU-driven renderer (the upstream
   proposal godot-proposals #6109). Rejected for now: a second renderer
   (D001), large conflicts with upstream, every material and feature
   (DDGI, path tracer, weather) to port.
3. A persistent per-instance GPU table next to the existing structures
   (slot per geometry instance, dirty-range uploads), then GPU culling over
   it into compacted per-bucket ID lists, then indirect submission. Chosen:
   each step is useful and testable alone (the table alone already cuts the
   per-frame 176-byte rewrite to 4-byte IDs), it reuses the MultiMesh
   source-ID remap and culling kernels (D003, D010), and unsupported
   instances keep the current path.
Parallel render-list preparation (roadmap Phase 4) is planned independently,
because the CPU path stays the fallback for skinned, transparent and other
excluded instances.
Benchmark evidence (`instance_bench`, docs/renderer/benchmarks.md, RTX
3060, Vulkan and D3D12): every case above 1k nodes is render-thread CPU
bound. At 100k nodes (84k visible, 16 draws) render CPU is 20.4 ms against
1.15 ms GPU; main-pass render-list preparation ("Setup 3D Scene") is 16.4 ms
of it, the threaded cull 1.2 ms, shadow lists 2.5 ms. The order holds. Task
1.0 splits the 16.4 ms before Phase 1 code, to tell how much the table alone
saves and how much needs Phase 2.

## D026 — The GPU instance table holds only view-independent instance data

2026-11-10. Implemented (Phase 1.2, opt-in
`rendering/geometry/gpu_instance_table/enabled`, default off).
Problem: the per-frame 176-byte `InstanceData` mixes three lifetimes.
`_fill_render_list` recomputes `flags` and `gi_offset` for every pass and
view (fade alpha from the camera distance, the dynamic/motion bit, lightmap,
lightmap-capture, VoxelGI and SDFGI bits; shadow passes overwrite them).
The compressed AABB, UV scale and texture-feedback index belong to the mesh
surface, not the instance.
Candidates:
1. Store the full 176-byte record per instance. Rejected: the per-view parts
   would need rewriting for every pass, which is the cost being removed, and
   per-surface parts would need one record per instance and surface.
2. One record per instance and surface. Rejected: multiplies memory and
   uploads by the surface count for data that is mostly per instance.
3. A 128-byte per-instance record (transform, previous transform, base
   flags with non-uniform scale, instance uniform offset, layer mask, slot
   generation, lightmap UV scale); per-view bits stay a small per-element
   value, per-surface values a per-draw value. Chosen.
Upkeep: change hooks in `set_transform`, `set_layer_mask`,
`reset_motion_vectors`, `age_out_motion` and `_geometry_instance_update`
mark the slot; records are rebuilt once per frame at the start of
`_render_scene` and only dirty ones are uploaded. Turning the setting on
rebuilds every slot; turning it off frees the buffer. Free slots hold zeroes
with an even generation.
Evidence: `devtools/test/instance_table.ps1` (Vulkan and D3D12): a
non-stalling readback every 30 frames equals the CPU copy through growth past
1024 and 2048 records, 200 moving nodes, layer and material changes, 300
frees with slot reuse, and an off/on rebuild (18 checks); a self-test mode
that drops one upload is reported. Cost: nothing in static scenes; with 2,000
scattered moved nodes, one `buffer_update` per range costs +2.9 ms GPU (task
1.3 replaces it). Not done: nothing reads the table yet (1.5).
Update 2026-11-10 (task 1.3): scattered dirty records are packed into one
upload (records plus slot list) and written by a compute scatter
(`gpu_instance_scatter.glsl`); up to 16 contiguous runs still use one
`buffer_update` each. `dynamic_20k` GPU 4.32 to 1.43 ms (table off: 1.41).
Both paths and the self-test pass on Vulkan and D3D12.
Update 2026-11-10 (task 1.4): the table is brought up to date at the start
of every scene render, not once per frame. The motion age-out of a render's
instances runs just before it (`RenderForwardClusteredPT::_render_scene`), so
a second viewport's age-outs reach the table before that viewport draws.
With validation on, every element `_fill_instance_data` writes is compared
with its record (transforms, previous transforms, uniform offset, layer
mask, lightmap scale, base flags as a subset of the element's flags); slots
still dirty are skipped. Self-test mode also leaves one record stale, which
this check reports. Fixture adds 100 teleports and a second viewport.

## D027 — No slot-ID render elements; eligible instances leave the CPU camera lists

2026-11-10. Decided from a measurement, before writing tasks 1.5-1.7.
Problem: the plan was to make render elements carry a 4-byte table slot
instead of the 176-byte `InstanceData`, with the scene shader reading the
table. Its payoff depends on what `_fill_instance_data` spends its 7.8 ms
on at 100k nodes.
Measurement (temporary build switch, `-Stages`, Vulkan, 100k nodes, main
pass): all work 7.80 ms; without the transform transposes 5.95; also
without mesh lookups 5.93; also writing 4 bytes instead of 176: 5.32. So
the record write is about 0.6 ms, the transposes about 1.9 ms, and the walk
over the elements (each points to a scattered surface cache and instance
object) about 5.3 ms. Same pattern in shadow passes and at 20k.
Candidates:
1. Shader read path plus slot-ID elements (1.5-1.7). Rejected: at most about
   2.5 of 20 ms, with scene shader changes, doubled pipeline variants and
   every material path to revalidate.
2. A GPU expand pass from small per-element records into the existing
   instance buffer. Rejected for the same reason: the CPU still walks every
   element.
3. Keep eligible instances out of the CPU camera render lists; per view, a
   compute pass culls them from the table, writes the existing 176-byte
   `InstanceData` for visible ones into a GPU-local buffer and fills one
   indirect command per draw bucket. Chosen (revised Phase 2): the CPU stops
   visiting them, and the scene shader stays unchanged.
Also: making the CPU walk cache-friendly (hot per-element fields in the
surface cache) is a separate, cheaper candidate for the CPU fallback path
(Phase 4).

## D028 — The GPU path writes its own instance buffer (binding 47)

2026-11-10, Phase 2.3/2.4.
Problem: D027 planned to keep the scene shader as it is, with the compute
pass writing the 176-byte instance data where the shader already reads it
(set 1, binding 2). That buffer is a dynamic persistent buffer, mapped by
the CPU and declared as a dynamic descriptor in the scene shader's layout.
On D3D12 such buffers live in an upload heap, which the GPU can only read:
it can be neither a UAV nor a copy destination.
Candidates:
1. Compute writes into the binding-2 buffer. Rejected: impossible on D3D12
   upload heaps, and the buffer is mapped once per frame by the CPU.
2. Compute writes a GPU buffer and copies into binding 2. Rejected for the
   same reason.
3. A second instance buffer at binding 47, chosen per draw by a scene
   shader specialization bit (`gpu_instances`, like
   `multimesh_gpu_culling`); every read of the instance data goes through
   one macro, `SCENE_INSTANCE(field)`. Chosen. Pipelines with the bit are
   compiled only for materials drawn by buckets; the ubershader evaluates
   the choice per read.
Also decided here:
- The table record grows to 160 bytes: world bounds for the frustum test,
  and two bits (moved this frame, which gives the dynamic flag, and can use
  SDFGI). Still view-independent (D026).
- Consistency within a render: the scene cull reads `gpu_drawn` when it
  culls, and the renderer may change eligibility later in the same render
  (dirty instances update there). Joins only take effect at the next cull
  (`gpu_instances_begin_cull`); an instance that leaves during a render is
  drawn by the CPU list that time. So no instance is drawn twice or missed.
- Instances in a view the GPU path does not cover (probe, mirror, XR,
  motion list, debug draw) go through the CPU list from the cull's
  separate list; nothing is lost when the path declines a view.

## D029 — GPU path index LODs: per-level visible IDs, instance data per member

2026-11-10, Phase 2.5 (first item).
Problem: a bucket whose surface has index LODs needs one draw per level,
each with its own index buffer and its own run of instances. The scene
shader finds an instance as push-constant base plus `gl_InstanceIndex`, and
D3D12 runs with base instance zero (`first_vertex_and_base_instance_mode`
is `DXIL_SPIRV_SYSVAL_TYPE_ZERO`), so the indirect command's
`firstInstance` cannot carry a GPU-computed offset: each level's start must
be known on the CPU.
Candidates:
1. Reserve a full bucket of 176-byte records per level. Rejected: memory
   is members times levels (about 100 MiB at 100k members and 6 levels).
2. Count, prefix and write in two passes, with a GPU offset in the command.
   Rejected: needs base instance, which D3D12 does not give.
3. The kernel writes each member's instance data once, at the member's own
   index, and appends the member index to a visible-ID list per bucket
   level (count IDs reserved per level, 4 bytes each). The scene shader
   reads `gpu_instance_ids[base + gl_InstanceIndex]` (binding 48) under the
   existing `gpu_instances` specialization bit. Chosen; non-LOD buckets use
   the same path with one level.
Also decided here:
- The kernel chooses the level with the CPU rule (`mesh_surface_get_lod`:
  distance from the main camera to the world bounds, model scale times LOD
  bias from the table record, which now keeps it in place of padding; the
  view's distance multiplier and threshold; orthogonal views use 1).
  Validation compares the GPU's per-level counts with the CPU's choice.
- One render element per bucket level, `lod_index` set, so the draw loop
  binds the level's index array as for CPU elements. A view with LODs off
  (threshold 0) appends level 0 only.

## D030 — GPU path in views with a motion pass

2026-11-10, Phase 2.5.
Problem: with FSR 2, DLSS or MetalFX the CPU list puts moved instances in a
motion list: the depth pre-pass draws them from the opaque list, the opaque
color pass skips them (the surface's inclusion mask) and a motion pass draws
them with motion vectors. GPU path elements share their bucket's surface, so
the mask on the surface cannot tell moved from resting members.
Decision: every bucket level has a second command and ID range for members
that moved; in such a view the kernel puts moved members there (and does not
set the dynamic flag, as the CPU list does not). Those elements are in the
opaque list, marked `gpu_motion` in the element info (the draw loop takes the
inclusion mask from it for GPU path elements), and again in the motion list;
the motion pass binds the GPU path's buffers. Only if an instance moved in
this or the last frame does the view get them (otherwise no empty motion
pass); the kernel then also files nothing as moved, so a wrong guess draws
the members in the opaque pass and the validation reports it.
Test: `gpu_instances.ps1` reads the velocity buffer back (a compositor effect
copies it through a compute shader) under TAA and FSR 2, with nodes that
alternate between two positions, four frames at one jitter phase. Values are
compared as numbers: D3D12 gave one zero vector the other sign in the GPU
path's pipeline. Moving nodes are kept clear of resting ones, since equal
depths let the draw order choose the vector and the GPU path orders a bucket
differently.

## D031 — GPU path buckets updated in place

2026-11-10, Phase 2.5.
Problem: every join, leave or surface change rebuilt the scenario's buckets
(collect, sort, upload): measured 7.6 ms at 100k members, 1.4-1.8 ms at 20k.
Games that spawn, despawn or change materials every frame would pay it every
frame.
Decision: buckets keep spare member slots (a quarter, at least 32) and a
free list. A key-to-bucket map places a joining surface in a free slot of
its bucket; a leaving instance's slots go back to the free list (the cull
skips free slots). Each instance chains its member slots, so leaving touches
only them and never its possibly freed surfaces. A bucket draws with the
surface of one live member; when that one leaves, the next view picks
another. `_mark_dirty()` frees an instance's surfaces at once, so it takes
the instance's members out there and places the rebuilt surfaces before the
next view. Changed member slots go up as contiguous runs. A full rebuild
remains for a new key, a full bucket, a mesh whose index counts changed
(checked on placement) and when free slots outnumber live ones.
Also fixed here: a transform that flips the mirror sign now refreshes the
instance (mirror is part of the key), and readback callbacks check a
lifetime flag (see kb mistakes, readback callback after renderer teardown).
Measured: `instance_bench` `churn_100k` (100 material changes per frame)
6.1-6.8 ms render CPU on the GPU path against 4.8-5.2 ms without changes;
`gpu_instances.ps1` replaces 20 nodes and swaps materials every frame for 60
frames with no rebuild.

## D032 — The GPU path draws what the scene cull found visible

2026-11-10, Phase 2.5.
Problem: the cull kernel tested the frustum and layers itself, so it ignored
`OccluderInstance3D` occlusion (and its count check failed in scenes with
occluders), and its widened frustum could differ from the CPU's at the edges.
The scene cull still runs for every camera and already lists the GPU path's
instances apart.
Decision: the scene cull threads also list each such instance's renderer
slot (`RenderGeometryInstance::gpu_cull_slot`, read next to `gpu_drawn` in
the same object); the renderer turns the slots into a bit per table slot and
uploads it per view (12.5 KiB at 100k slots); the kernel draws a member when
its bit is set. Frustum, layers and occluders come from the CPU, exactly.
Rejected: building the bits on the render thread from the pointer list:
+2.7 ms at 100k (84k pointer chases); from the cull's slot list it is
+0.2 ms. GPU occlusion (HZB) can later clear bits or test after them.

## D033 — Directional shadow casters on the GPU path

2026-11-10, Phase 6.1.
Problem: with the camera passes on the GPU path, the CPU shadow lists (fill,
sort, instance data per cascade) were most of the remaining render CPU:
about 3.0 of 5.2 ms at 100k nodes and 4.6 of 5.3 ms at 20k.
Decision: as for the camera (D032), the scene cull lists the GPU path's
casters apart per directional cascade, with their slots. Per shadow pass the
renderer uploads the slot bits and a fresh copy of the commands, runs the
cull kernel with the shadow pass's LOD inputs (shadow threshold scale and
distance multiplier, main camera position), and appends one element per
bucket level to the pass's range of the shadow list. The draws use the
depth twins (shadow mesh index counts). Each pass has its own commands,
visible IDs and bits. The shadow passes draw from the camera view's
instance data: the camera kernel writes every live member's record, visible
or not, and the shadow kernels write none (shadow depth shaders read none of
the view-dependent flags). A first version wrote a second copy per shadow
pass: 22 MiB more at 100k and 4 writes per caster per frame. Views the
GPU path does not draw (no camera GPU view in this render) put the casters
back into the CPU list; instances that left during the render go to every
cascade's CPU list, clipped where they do not belong. Positional lights
(spot, dual paraboloid and cube omni) followed the same day (6.2): their
shadow culls split the casters the same way, and every shadow pass of a
render has its own commands and IDs.
Also: with many buckets for few listed instances (one per material), a
view marks the buckets its listed instances use and skips the others
(`unique_10k`: 30,003 shadow draws, mostly empty, down to the CPU path's
count; camera draws 10,001 to 8,776).
Measured (Vulkan, render CPU p50, GPU path before / after): `massive_100k`
5.2-5.4 / 2.8-2.9 ms, `shared_20k` 4.3-5.5 / 0.7-0.8 ms, `lights_20k`
3.3-4.2 / 0.75 ms, `dynamic_20k` 4.4-5.4 / 2.1-2.2 ms, `unique_10k`
8.1-8.4 / 7.1-7.7 ms, `small_1k` 0.31-0.34 / 0.25-0.26 ms. Shadow A/B is
part of every image comparison of `gpu_instances.ps1` (the sun casts), and
casters behind the camera darken 447 pixels in view.

## D034 — HZB occlusion of GPU path instances for the colour passes

2026-11-10, Phase 5.1. Opt-in `rendering/geometry/gpu_instances/occlusion`
(default off).
Problem: the GPU path draws every instance the scene cull lists, so
instances hidden behind real geometry (not `OccluderInstance3D`) are shaded
in the opaque pass. The HZB of D023/D024 rejected only CLOD groups and
MultiMesh instances.
Decision: after the depth pre-pass and the shared HZB build, a second mode
of `gpu_instance_cull.glsl` (`MODE_OCCLUDE`) runs over the members with the
camera run's push constant, so it chooses the same command (index LOD,
motion) for each drawn member. It tests the member's world box from the
table record with `hzb_box_occluded()` (one world-to-clip matrix per view,
rounding bound abs(P) * abs(V) times the box's world magnitude, 128 float
epsilons) and appends the members it cannot prove hidden to a second set of
commands and IDs with the same layout (per scenario, uploaded from the CPU
copy, not GPU-copied: kb mistake `d3d12-buffer-copy-then-compute-write`).
The opaque and motion passes bind those; the depth pre-pass, shadows,
validation and render info keep the camera run's. Same conditions as the
other HZB users (depth pre-pass, no MSAA, compositor, render region,
fragment depth or stencil writers, also among the buckets). Statistic
`gpu_instances_occluded`.
Evidence: `gpu_instances.ps1` wall phase on Vulkan and D3D12: 279 surfaces
occluded, 0 with the setting off, image equal to the CPU path; the setting
stays on for the later phases (positional shadows, offscreen casters, more
views), all equal. Cost without occluders (instance bench, Vulkan, GPU p50):
`small_1k` +0.08 ms, `shared_20k` +0.10 ms, `massive_100k` +0.08-0.12 ms;
render CPU +0.03-0.07 ms.
Found on the way: `_fill_instance_data()` counted earlier passes' elements
again in the render info when the instance buffer was remapped mid-frame
(upstream has the same order). On the GPU path a shadow pass with no CPU
elements leaves the buffer unmapped, so the next pass remaps; a dual
paraboloid light counted its 92 bucket elements again in some frames. The
fixture's shadow render info comparison now sums 12 frames, because which
passes a frame draws repeats and the GPU path's count arrives late.
Not done: depth-pass savings (a two-pass history like D023); a bench scene
with occluders for the GPU path.
