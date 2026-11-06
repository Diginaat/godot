# Native large-world integration architecture

Status: initial design, 2026-11-05.

## Ownership and data flow

```text
Godot scene/resources/importers (unchanged source of truth)
  -> RenderingServer / RendererSceneCull (coarse visibility and dependencies)
  -> MeshStorage (source geometry, optional derived cluster payload)
  -> inherited Forward+ renderer (view/pass eligibility and submission)
  -> RenderingDevice compute -> indirect arguments -> ordinary material raster
```

No second scene database exposed to gameplay, renderer, resource loader, NVRHI
dependency or runtime connection to another engine. GPU tables are rendering caches keyed by
Godot resource identity/generation, not independently owned scene objects.

## First slice and acceptance

First practical slice: harden and exercise the **existing native indirect
MultiMesh path**. Test explicit counts, zero, `-1` (all allocated instances),
multiple surfaces, GPU-written counts and release. Preserve initially zero
indirect counts: GPU producers own initialization unless the CPU setter is used.
No new setting or renderer is needed for this slice. It is not a GPU culler.

Pass condition: no negative sentinel reaches an unsigned indirect instance
count; a real RD buffer test proves the command bytes; GPU compute can write
bounded counts and Godot can render those through its existing materials.
Build/test failures block calling the slice validated.

## Implemented slice: native MultiMesh frustum visibility

Experimental opt-in project setting:
`rendering/geometry/multimesh_gpu_culling/enabled` (default `false`). A project
setting avoids changing the MultiMesh resource ABI or taking ownership of
externally managed indirect MultiMeshes. Per-resource eligibility overrides are
not introduced in this first version.

Implementation:

- `servers/rendering/renderer_rd/forward_clustered/multimesh_culling.cpp`:
  eligibility, per-surface metadata, per-viewport buffers and compute recording.
- `servers/rendering/renderer_rd/shaders/forward_clustered/multimesh_cull.glsl`:
  transformed conservative AABB versus six local-space frustum planes; block
  prefix scan and stable scatter retain source draw order and write bounded counts.
- `render_forward_clustered.{h,cpp}`: prepare after native render-list setup,
  reuse visibility in main-view depth/opaque/motion passes, retain ordinary
  submission elsewhere, release buffers with viewport data.
- `scene_forward_clustered_inc.glsl`: set 1 binding 46 visible-ID storage and
  source-ID remap for transform, color/custom-data and `INSTANCE_ID` lookup.
- `scene_shader_forward_clustered.{h,cpp}`: specialization bit 9 compiles the
  remap out of ordinary specialized pipelines; track unsupported custom vertex
  transforms for fallback.

The CPU processes surfaces, not individual instances. GPU transforms are neither
read back nor compacted/copied. Current/previous transform-history offsets remain
source-indexed. GPU counts are not read back by the production culler. Output
capacity reserves the surface's input count rounded to 64, so compaction cannot
overflow. Stable ordering preserves coplanar colored/custom-data instance ties.

Source buffers and externally owned command RIDs are never modified. Each view
has persistent command/input/ID buffers; RD tracks upload/compute/indirect-read
dependencies and deferred destruction. Growth invalidates dependent uniform sets
through normal RD ownership. Two timestamp markers bracket enabled work.

A MultiMesh whose world bounds lie inside all six frustum planes skips GPU
culling and keeps its direct draw: no instance could be rejected, so the three
dispatches are pure cost. A too-small custom AABB can only cause extra drawing
(source behavior); a non-finite bound keeps the culling path. Measured on the
valley benchmark: 0.1–0.18 ms less render-thread CPU in a view where 112 of
172 visible patches were fully inside; no change where none were.

Per-instance GPU LOD (2026-11-06). Godot picks one index LOD per MultiMesh,
from the nearest point of its bounds, so a large MultiMesh draws every instance
at the detail of its closest one. For surfaces with index LODs the culling
kernel now picks a level per visible instance with Godot's own rule (edge length
times instance scale against the distance to the instance box). Level 0 is the
CPU-selected LOD; the farthest point of the bounds gives the coarsest LOD any
instance can need. Up to four levels are bucketed over that range (exact when
it spans at most four LODs, else LODs first + round(b * span / 3)), so an
instance can get a finer LOD than its own CPU choice, never a coarser one. Each
level has its own visible-ID range and indirect record; the draw binds the
level's index array per record. A packed one-byte-per-level counter keeps the
ordered single-pass scan. Surfaces whose range is a single LOD (including a
reduced one) are culled at that LOD and still use the fully-inside skip.

Jittered views (2026-11-06). TAA and upscalers add the jitter to NDC x/y
(`Projection::add_jitter_offset`). Culling divides the projection's x/y rows by
1 + |jitter|, which widens the frustum to |ndc| <= 1 + |jitter| and so contains
the jittered frustum of that frame: culling stays exact. CLOD selection uses the
same widened planes; HZB occlusion stays off under jitter, because its group
rectangles use the unjittered projection.

Shadow passes (2026-11-06, opt-in `multimesh_gpu_culling/shadows`, default
off). After shadow instance data is filled, one batch culls every
single-frustum shadow pass (directional cascades, spot lights) against its own
shadow camera, with separate per-viewport buffers because shadow draws run
between the main view's culling dispatch and its opaque draws. The near plane
never culls (pancaking keeps casters in front of it). Dual-paraboloid passes and
cubemap omni shadows rendered on their own keep direct draws. Shadow LOD follows
the main camera, as in Godot. Normal bias is applied when sampling, not to
caster vertices, so the planes need no bias margin.

Kernel binding: shared buffers are set 0, bound once per mode; the MultiMesh
transform buffer is set 1 and exists only in the cull mode. The scan and scatter
modes bind nothing per draw.

Eligibility is intentionally conservative: conventional opaque 3D MultiMeshes,
identical main/shadow surface representation, ordinary model/view transforms and
mono raster views. Vertex displacement, custom model/view/projection transforms,
transparency, stencil, particles, GPU-owned indirect counts, 2D instances,
mirrors, reflection captures, XR and double builds keep conventional rendering.
Unsupported configurations are fallback, not errors.

Cache limits: at most 2^23 ID slots (32 MiB IDs, 32 MiB rank scratch and 0.5 MiB
block scratch), 65536 draws (9 MiB metadata and 1.25 MiB commands), per viewport,
excluding source geometry and uniform-cache
overhead. Over-budget surfaces and input counts beyond the device's X-dispatch
limit remain direct. Allocation/pipeline failure does not activate culled draws.
This first version repeats visibility per eligible surface rather than sharing
a heterogeneous mesh-wide list; optimize only after measuring.

Compaction uses three GPU stages: local visibility/ranks, block-prefix scan,
ordered scatter. No subgroup operations or CPU count feedback are required.
Devices without 256-invocation compute groups keep conventional rendering.
Scratch is cached until viewport resize/destruction, including while toggled off.

Final production editor rebuild and focused kernel/native-renderer A/B tests
pass on Vulkan and D3D12, including viewport resize and resource replacement.
Standard PhysX/PT/DDGI smoke-suite rerun passes. See
[validation](PERFORMANCE_BENCHMARKS.md) for exact limits and results.

## GPU geometry statistics

`RenderingServer.viewport_get_gpu_geometry_statistics(viewport)` exposes counters
from the culling, CLOD selection and HZB kernels: MultiMesh instances tested and
visible (also per GPU LOD level), fully visible surfaces, CLOD groups tested, selected and visible, visible
triangles, and HZB-rejected groups and triangles. Kernels always add into a
per-viewport 24-word buffer (shadow-pass culling words from word 12). Clearing and an asynchronous
`buffer_get_data_async` copy happen only for 120 frames after a call, so there is
no setting and no readback when nobody polls. Values lag a few frames and are
tagged with their frame. See `kb/project/gpu-geometry-statistics.md`.

## Further MultiMesh visibility work

Opt-in eligibility, off by default; no changes to ordinary MultiMesh semantics.
Use persistent source transforms and a visible source-ID list, not packed
transform rewrites. Preserve `INSTANCE_ID`, custom data and previous transforms.
An object-level material with vertex displacement/billboarding must supply
conservative bounds or remain conventional. Exclude unsupported deformation,
transparency and path-traced geometry initially. Never infer safety merely from
an opaque material flag.

Per-view/per-pass outputs contain a visible-ID range and indirect command range.
They belong to the renderer's submitted pass lifetime, **not one global command
per MultiMesh**: main view, shadows, mirrors, reflection captures and other
viewports may render the same resource. Commands and binding ranges cannot be
rewritten underneath earlier draws. Reset counts before appending; capacity at
least input count or explicit conservative fallback. One source instance emitted
at most once per output. No CPU visibility readback on the critical path.

Begin with ordinary indexed indirect surface draws. Additional surfaces retain
separate material calls; do not claim one draw for an entire heterogeneous world.
Use normal Godot shadow passes until pass-specific culling is proven. Never use
main-camera rejection as shadow visibility. CPU coarse scene bounds still apply.

## Proposed geometry representation (not yet a serialized ABI)

- Optional derived resource/cache per mesh surface: conservative local cluster
  bounds, self/parent group errors and bounds, page dependency/index ranges.
- Retain Godot's full required attributes and material boundaries. A
  position/normal/RGB-only vertex is insufficient.
- Cluster payload uses shared RD-managed index/vertex pools; integer IDs/offsets,
  not pointers. Define explicit CPU/GPU sizes and static assertions when implemented.
- Page metadata records source/importer version, attribute layout, bounds, root
  dependencies and payload sizes. No persistent on-disk format before cooker tests.
- CPU resource lifetime owns root data and source fallback. Renderer owns
  per-view work, indirect commands and visibility history.
- Start with conservative 64-vertex cluster experiments; do not lock triangle
  count to another engine's measured choice without Godot measurements.

Godot's existing surface index LOD remains available; never reinterpret it as a
DAG. Import clusters as optional derived data, invalidate on mesh changes, fall
back on missing/unsupported cache versions. Dynamic/skinned/transparent geometry
continues through established paths.

## HZB invariants

Use reversed-Z farthest depth (min reduction) with exact source coverage. Cover
all touched texels, handle near-plane crossings conservatively and include
jitter/rounding margins. Start current-frame-only. Add prior-frame scheduling
only with same-frame rejection recovery. Disable history on invalid projection,
resize, view changes or unhandled occluder motion. History is per eye/view.
Empty depth and any uncertain test must retain geometry. Never build occlusion
from alpha-blended surfaces as solid occluders.

## Streaming invariants

Roots remain resident; missing child data chooses complete resident parent cuts.
Bound upload bytes, metadata, roots, pending I/O, scratch, pool slack and payload
separately. Feedback uses optional delayed RD readbacks; skip unavailable samples
rather than wait. Worker loads/cooks bytes; rendering thread creates/patches RIDs.
Publish residency only after upload dependencies are recorded. Retire ranges
according to actual submitted RD lifetime, not a fixed frame-count guess.
Eviction cannot leave descendant-only cuts. Device loss/reimport/resource free
invalidates generations and pending work safely.

## Implemented slice: in-memory CLOD paging

`rendering/geometry/cluster_lod/streaming/enabled` (default off, read when a
mesh's cluster data is attached) streams resident-CLOD surfaces through one
shared GPU index pool of `streaming/budget_mb`
(`storage_rd/mesh_cluster_streaming.cpp`):

* A page is one cluster group's indices. Pages are allocated first fit from the
  pool's free ranges, which coalesce on free. Root groups are pinned; a surface
  whose roots do not fit stays `incomplete` and renders conventionally.
* The residency table (`x` flags, `y` first child, `z` pool offset) is the same
  one the residency-aware selection kernel already reads, so the GPU cut only
  descends into resident children and always has a complete resident parent.
* Selection writes requested and used bits per group. The render thread reads
  them back asynchronously (`buffer_get_data_async`, one readback in flight,
  results tagged with the request-layout version and dropped if stale), never
  waiting on the GPU.
* Requests load coarse groups first, at most 1M indices per frame. When the pool
  is full, least recently used groups that were not drawn in the last two
  feedback frames are evicted; otherwise the request is deferred and the
  coarser cut stays.
* A budget change rebuilds the pool and reloads the roots. The old buffer is
  freed through RD's deferred destruction, so in-flight frames keep their data.

Met invariants: roots resident, no descendant-only cuts, bounded uploads,
non-blocking feedback, renderer-thread RID ownership. Not yet met: pages come
from CPU memory (no disk I/O or worker loads), no resource generations across
reimport, and pool slack/metadata are not budgeted separately.

## Implemented slice: GPU-generated grass

`GrassField3D` (`scene/3d/grass_field_3d.cpp`) ports the idea of mesh-shader
grass (blades as a pure function of patch, blade index and seed; nothing
stored per blade) without mesh shaders, which RenderingDevice does not expose:

* Every frame the node queues one compute pass with
  `RenderingServer.call_on_render_thread`. One invocation per 0.5 m patch of a
  camera-centred square grid rejects patches outside the radius, outside the
  grid, at zero density or outside the camera frustum (a conservative sphere
  from the patch's corner heights and the tallest leaning blade).
* A visible patch reserves its blades with one atomic add and writes 3x4
  transforms and colours straight into an indirect MultiMesh's instance
  buffer (`multimesh_get_buffer_rd_rid`); a one-invocation second mode clamps
  the count to the buffer and writes it into the indirect draw commands
  (`multimesh_get_command_buffer_rd_rid`). Drawing is Godot's ordinary
  indirect MultiMesh path, so materials, lights, fog and depth work unchanged;
  GPU MultiMesh culling already excludes indirect MultiMeshes.
* Heights and density come from optional grids interpolated over the same two
  triangles per cell as the valley's terrain mesh, so blades stand exactly on
  it. The blade count per patch falls continuously over the outer radius and
  the last blade narrows, so the count changes without popping.
* The shader is compiled at runtime from GLSL (`shader_compile_spirv_from_source`)
  and all RenderingDevice resources live in a reference-counted state object
  bound into each queued call, so a node freed while work is queued cannot
  leave dangling render-thread state; its resources are freed on the render
  thread. Memory: 64 bytes per blade of capacity plus the grids.

Limits: blades exist for the viewport's current camera only (shadow cameras
and other viewports see the same set, so shadows are off by default); no wind,
so motion vectors stay exact; no editor-viewport preview.

## Precision, capabilities and fallback

Do not change Godot coordinate/physics policy. Keep chunk-local floats and
Godot-owned transforms; subtract high-precision origins before GPU float culling.
Preserve compensated rendering in double builds. Perspective/orthogonal,
asymmetric and XR frusta need separate proof; unhandled cases use conventional
rendering. Compatibility stays unchanged. RD compute + indirect is baseline;
mesh shaders, 64-bit atomics and vendor extensions remain optional future work.

## Dependency order and stop gates

1. Command correctness + reproducible baseline (first slice).
2. Stable-ID per-view frustum culling; image-equivalent to uncull reference.
3. Cluster import/cut/indexed raster; cracks, materials, shadows and memory tests.
4. HZB correctness including motion/cuts/resize and multiple viewports.
5. Resident-parent streaming under forced budget pressure and resource destruction.
6. Vegetation patches and distant proxies; equivalent coverage/lighting tests.
7. Full percentile/memory/scalability comparisons, backend and double-build matrix.

Each gate requires a working build and focused evidence, not only interfaces or
debug counters. No claim of completing stages 2–7 is made by the foundation.

## Virtual geometry scope

User explicitly requested Nanite-like virtual geometry, cluster DAG CLOD and
far-field voxels. These are required subsequent slices, not alternate names for
the instance culler. See [virtual geometry design](VIRTUAL_GEOMETRY.md). The
cluster cooker, resident CLOD raster, HZB, in-memory paging and voxel splat
proxies are implemented as separate opt-in slices; see
[port status](PORTING_STATUS.md) for what each one proves.
