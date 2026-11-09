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
Benchmark evidence: pending (`instance_bench` baseline,
docs/renderer/benchmarks.md). Revisit the order if the baseline shows that
render-list work is not the main render-thread cost.
