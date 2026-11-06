# Native virtual geometry, CLOD and distant voxels

Status: native CPU cooker/resource, ArrayMesh attachment, GPU selector and resident
raster and opt-in scene-import cooking implemented. CLOD paging from memory or a page file, static voxel
proxies and GPU voxel levels for the far field are implemented and experimental.
Explicit project scope added 2026-11-05.
“Nanite-like” describes virtualized hierarchical geometry, not Unreal code or
binary/API compatibility. No new third-party renderer or runtime dependency.

## Implemented cooker

`MeshClusterData` is a native Resource, with generation supplied through the
existing meshoptimizer module. `modules/meshoptimizer/cluster_builder.cpp` uses
the already-vendored MIT meshoptimizer 1.2 (no dependency update): meshlet
construction, spatial partitioning and attribute-aware border-locked
simplification. `scene/resources/mesh_cluster_data.{h,cpp}` owns checked versioned
data, serialization and a CPU reference selector. Class help is in
`doc/classes/MeshClusterData.xml`.

This adapts the cluster/group principle, not another engine's demo cooker text. The
first hierarchy is a rooted group tree rather than a cross-linked DAG. Each
parent stores a simplification of its complete child region and accumulated
absolute error, with bounds enclosing child geometry. Leaves preserve source
triangles. Selection traverses roots and either emits a complete group or all
its children, so rounded bounds cannot create overlapping cuts or holes.
Projection uses double intermediates and overflow-safe distance evaluation.

All LOD indices reference existing source vertices; no original vertex/material
buffer is rewritten. Position, normal, tangent, color, UV and UV2 metrics are
supported. Blend shapes, skinning, custom channels and non-triangle surfaces
currently return unsupported, preserving the conventional source path.

The resource stores typed arrays (not an assumed GPU struct ABI). Validation
rejects cycles, bad parent/root links, invalid errors/bounds, orphan/overlapping
record ranges and out-of-range source indices. A failed build/load preserves
previous valid data. Reference selection is for tests/inspection, not a proposed
CPU per-instance render loop. No renderer or voxel system is enabled merely by
constructing this resource.

ArrayMesh attachment follow-up: optional `cluster_data` is serialized after
source surfaces; `generate_cluster_lods()` cooks and attaches without changing
the source RID. Source-array SHA-256 signatures prevent accidental attachment of
another mesh's cache even when counts match. Matching reads decoded source
arrays at attachment/loading, never per frame. Old unsigned caches can still be
inspected but must be regenerated to attach. Signatures are stale-data checks,
not authentication of user-supplied hierarchy bounds/errors.

Source geometry/attribute mutations detach derived data. Material/name edits
retain it. A changed shared hierarchy is rechecked and detached from old owners
if its source differs. Resource and future rendering storage share one cache
validator in `servers/rendering/storage/mesh_cluster_data_format.h` rather than
introducing a scene-layer dependency into the rendering server. Attachment alone
has a separately opt-in native GPU raster consumer (see below). Production build,
focused Vulkan/D3D12 lifecycle tests, class-reference checks and standard smoke pass.

Initial curved-grid fixture: 13,824 source indices, 82 groups, 127 clusters and
3,456 root-cut indices. Full cut coverage, external/internal boundary topology,
source preservation, resource roundtrip, corrupt-data rejection and extreme-finite
projection inputs pass in headless and the Vulkan/D3D12 runners. Production
build passes; no speedup claim.

## Incremental implementation contract

Current-frame HZB baseline is now implemented for resident CLOD opaque commands.
`rendering/geometry/cluster_lod/occlusion_enabled` defaults false. A complete
selected-cut depth prepass precedes an independently rebuilt reverse-Z minimum
(farthest) depth pyramid; the existing SSR maximum-depth pyramid cannot prove
occlusion. Power-of-two padding is clear depth zero, preserving NPOT edges and
holes. Projected bounds include a conservative float-operation interval which
grows under large world-coordinate cancellation. Near/clip/screen uncertainty,
non-finite intervals and depth ties retain geometry. No previous visibility is
used, and each new frame regenerates selected commands before depth.

Only opaque instance-count words can be cleared; source indices, depth-prepass
commands already submitted, shadows and RT remain unchanged. This saves no depth
work and currently does not occlude MultiMesh IDs. MSAA, custom compositors,
partial render regions, opaque custom fragment depth/stencil, absent prepasses,
unsupported R32F storage or a conservative 64 MiB viewport mip budget keep
unoccluded CLOD. HZB inputs add at most 384 KiB per viewport (4096×96 bytes).
No production CPU readback or performance improvement is claimed.

Vulkan/D3D12 seven-part matrices pass: every GPU mip equals CPU min-depth
reduction, guarded commands reject hidden groups, touching/clipped/edge and
uncertain groups stay visible, one-pixel holes recover. Native tests preserve
source/fine and shadow pixels across three views, moving occluders, resizing,
coarse/fine transitions, MSAA/custom-depth fallback and source mutation. A
100000-unit shared world/camera translation retains uncertain groups without
pixel changes versus conventional source rendering. Final standard smoke passes.

One editor --import shutdown failed while scanning new scripts (null EditorNode
singleton/0xC0000005); isolated retry, five repeated imports and subsequent full
backend suites passed. Root cause is unresolved; no automatic retry masks it.

Resident-raster baseline: ArrayMesh attachment forwards checked
hierarchies to RD MeshStorage. Per-surface 32-bit alternate index buffers still
reference original native vertices; group records match the tested compute ABI.
Vertex/attribute/skin/index region updates and normal surface destruction retire
resident data through RD, which handles submitted-reader lifetime.

Opt-in `rendering/geometry/cluster_lod/enabled` prepares viewport-owned command
buffers and submits individual indexed indirect commands through existing native
depth/opaque shaders, vertex arrays and materials. Group error is scaled by a
conservative model-basis Frobenius bound, with nearest camera-forward depth.
Source shadows, RT, transparent and other passes retain source geometry.

Initial exclusions: deformed/skinned/blend-shape or batched instances, multi-pass
materials/overlays, custom
vertex/model/projection transforms, alpha/stencil/point-size shaders, selected
source LODs, mirrors/reflections, jitter, XR and double
builds. Unsupported work renders conventionally. Resident budgets: 4096
groups/surface and 64 MiB index/group bytes per mesh; viewport output budgets:
65536 groups and 4096 draws. Each surface's group records are submitted with one
multi-draw indirect call where RD reports `SUPPORTS_MULTI_DRAW_INDIRECT` (Vulkan
`multiDrawIndirect`, D3D12 always; Metal reports false). Other devices keep one
indirect call per record. In the valley benchmark this cut CLOD's render-thread
CPU overhead from +0.17–0.31 ms to +0.02–0.06 ms p50, with unchanged GPU time
and byte-identical CLOD pixels (see the benchmark document). Unselected records
still reach the GPU with zero instances; compaction with an indirect count is a
later option, only after measurement.

Native raster A/B fixture added for fine pixels, StandardMaterial3D/ShaderMaterial,
multiple surfaces/views, reflected nonuniform transforms, resizing, coarse/fine
transitions, source/RD index mutations, recook, custom vertex fallback and
destruction. Production build and Vulkan/D3D12 five-part runners pass. Coarse
pixels visibly differ from full geometry; returning to fine reproduces source
pixels exactly. A nonuniform-transform top-down floor crop contains measurable
cast shadow and remains byte-identical at coarse CLOD; conventional shadow draw
counts stay nonzero and unchanged. Custom vertex and material next-pass fallback
both pass. Final standard PhysX/PT/DDGI smoke rerun passes.

Scene-import follow-up: `meshes/generate_cluster_lods` defaults false. The native
scene importer cooks after lightmap unwrap, source LOD generation and index
optimization, before saving embedded/external mesh resources. Shared meshes are
cooked only on first conversion; unsupported deformation/custom channels or an
unavailable cooker warn and preserve conventional geometry instead of failing
scene import. With `meshes/cluster_lod_page_files` the importer also writes one
page file per cooked mesh into the imported folder before the mesh is saved and
lists it as a generated file, so it is exported with the scene (D019,
`import_paged` fixture). The saved mesh keeps only the roots' indices (D020);
without streaming the renderer reads the whole file at setup.

Separate optimized shadow meshes are now compatible with resident CLOD: main
camera depth uses source vertices with hierarchy indices; actual shadow passes
continue to bind the independently welded shadow resource. The native raster
fixture reverses shadow vertex numbering to detect incorrect cross-binding.
Both Vulkan/D3D12 six-part matrices pass, including real glTF imports with enabled,
disabled and unsupported morph options, saved signatures, preserved shadows and
shared mesh identity. Final standard smoke follow-up passes. The expanded runner
requires a 300-second outer tool budget on this machine; the initial 120-second
budget expired, while targeted and complete longer-budget runs pass.

GPU selector precursor: `mesh_clod_select.glsl` evaluates group projected error
using nearest camera-forward depth (or distance-independent orthographic scale).
Each selectable group checks every ancestor: any selected ancestor vetoes its
descendants. This yields the same exclusive cut as root traversal without a
local traversal stack. It produces bounded native five-word indexed commands,
with instance count zero or one, and applies pulled-back frustum covectors.
Non-finite projected comparisons conservatively refine. Initial Vulkan/D3D12
compute oracle tests pass; this alone does not submit native scene draws.

1. **Cook and validate cluster data.** Use existing meshoptimizer capabilities
   where sufficient; first compare the vendored 1.2 API with the source engine's
   1.3 cooker requirements. Generate per-surface clusters and simplification
   groups, preserving material boundaries, attributes and locked external borders.
   Unit tests must prove index ranges, conservative bounds, error monotonicity
   and complete parent/child coverage. An empty data resource is not this step.
2. **Resident CLOD raster path.** Keep ordinary Mesh resources authoritative.
   Optional import-derived data contains local bounds, group self/parent errors,
   hierarchy links and vertex/index ranges. GPU screen-space-error selection
   emits a non-overlapping complete cut. Indexed indirect raster feeds existing
   Godot material/depth/shadow systems; unsupported surfaces retain source Mesh.
   Begin all-resident and static opaque; no stream paging or voxel dependency.
3. **Visibility correctness.** Extend proven source-ID visibility with cluster
   frustum tests and conservative current-frame depth hierarchy, then two-pass
   temporal occlusion recovery. Measure complete frame time, not only the cull
   dispatch. Cracks, disocclusion and shadow correctness precede speed.
4. **Virtual payload pages.** Add versioned import cache and dependency tables.
   Coarse root cuts stay resident; missing child pages choose resident parents.
   Visibility feedback requests pages asynchronously. Budget payload, metadata,
   roots, staging, deferred-retirement slack and pending I/O separately. Eviction
   is safe only after submitted GPU readers retire through Godot RD lifetime.
5. **Distant voxel representation.** Derive sparse, coverage/centroid/normal-aware
   data from the same supported source assets. Evaluate Godot-native distant
   proxy raster first; do not require a new 64-bit atomic visibility renderer.
   Compare voxels with triangle CLOD at matching distance, lighting, materials,
   shadows, temporal stability and silhouette coverage. Reject voxel variants
   that trade away unacceptable quality or increase total frame time/memory.

## CLOD invariants

- Parent/self error bounds are monotonic and measured in the same projected
  convention; changing residency cannot leave holes or draw overlapping cuts.
- Cluster/group bounds enclose retained geometry under supported transforms.
  Non-uniform scales use conservative error expansion, not a uniform-scale
  shortcut. Chunk-local geometry keeps Godot's high-precision origin policy.
- Cluster selection and instance identity are separate domains. A visible
  cluster record references original instance/surface/material IDs; moving a cut
  must not change shader `INSTANCE_ID`, custom data or transform history.
- Fine/coarse transitions require temporal image tests; hard distance thresholds
  alone are not “continuous” quality. Avoid introducing geomorph promises before
  the representation supports them.

## Fallback, asset workflow and distant-band requirements

Source Mesh surfaces remain loadable and drawable without derived data.
Importer changes must be opt-in/versioned and must not invalidate existing
assets. Eventually Blender/environment imports can cook suitable static surfaces
automatically; artists do not assign cluster IDs. Skinned, transparent, displaced
and unsupported materials remain conventional until specifically supported.

Near/CLOD and voxel bands must have common conservative visibility, stable world
alignment, bounded residency and matched material/lighting semantics. Transition
regions need measured coverage preservation; do not assume prototype voxel
splats meet Godot's material, shadow or reflection requirements. Dense vegetation
requires explicit wind/history and authored-density treatment.

## Implemented far-field proxies

`VoxelProxyBuilder` (`scene/resources/voxel_proxy_builder.cpp`) builds a
static far-field stand-in from meshes and MultiMeshes:

* Triangles are split into equal-area sub-triangles no wider than
  `sample_spacing`; each sample carries its area, face normal and linear albedo
  (material colour times texture, times instance and vertex colour when the
  material uses vertex colour).
* `build(bounds, resolution)` merges samples per voxel cell: area-weighted
  centroid, colour and normal. The mean normal's length measures flatness;
  opposite faces cancel, so volumetric cells (foliage) lean towards up.
* Each cell becomes one camera-facing quad. Its half size is
  `0.5 * voxel * sqrt(min(coverage, 1))`, where coverage is the sampled area
  over one voxel face, so sparse cells stay sparse at a distance.
* The splat shader expands quads in the vertex stage from `UV` (corner) and
  `UV2.x` (half size). Quads are wound clockwise towards the camera; a back
  face would flip `NORMAL` under `cull_disabled` and lose direct light.

The valley fixture swaps patches beyond 180 m for proxies (`--far-field`,
key P). The switch cross-fades over 176-184 m with Godot's visibility-range
fade (`VISIBILITY_RANGE_FADE_SELF` on both sides, same range and margin):
sources fade out and proxies fade in with smoothstep alphas that sum to one,
and only patches inside the band draw in the alpha pass. `--far-hard-cut`
keeps the earlier opaque 4 m overlap for comparison. Static proxies have one
voxel size; shadows and reflections use the same splats without a separate
representation.

## GPU voxel levels

`VoxelProxyBuilder.build_field(bounds, resolution, brick_resolution)` builds a
`VoxelFieldData` (`scene/resources/voxel_field_data.cpp`). It ports a reference
engine's voxel levels:

* Level 0 accumulates the samples per finest cell. Each coarser level merges
  2x2x2 children: area summed, centroid, colour and normal area-weighted.
  Levels go down to one voxel per brick.
* Bricks list their voxels per level (`bricks`: packed brick coordinates, then
  first and count per level). Voxels are sorted by brick, level and cell.
* A voxel takes 16 bytes: cell in the brick (6 bits per axis), centroid
  offset in 1/16 voxel (4 bits per axis), sRGB colour, octahedral normal
  (signed 16-bit) and half size (coverage-scaled, as in the static proxies).

`VoxelField3D` (`scene/3d/voxel_field_3d.cpp`) draws it. Each frame one compute
workgroup per brick:

1. tests the brick's bounding sphere against the frustum;
2. picks the coarsest level whose voxel size times pixels per unit is at most
   `voxel_pixels` at the brick's nearest point (perspective or orthogonal);
3. decodes that level's voxels, culls each splat against the frustum, and
   compacts the survivors in shared memory;
4. reserves room with one global atomic per 64 voxels and writes each
   instance (scale and position rows, colour, normal in custom data).

A second dispatch clamps the count into the indirect MultiMesh command. One
level per brick means no gaps or double coverage between levels. The splat
material faces the camera and discards outside a disc with the square's area.
The instance buffer grows with the count read back from the GPU, up to
`get_max_splat_count()`. Outside its visibility range a field draws nothing
and skips its dispatch. `VoxelFieldData.select_level` and `count_splats`
mirror the GPU selection on the CPU, and the `voxel_field` fixture checks
that both produce exactly the same splats.

The valley flies with one field per far patch at 64 voxels per edge (0.75 m
finest) and 2 px (`--far-gpu`; `--far-static` for the static proxies,
`--far-res=N`, `--far-px=X`; `--far-compare` writes view images for the
image metric). Limits: no shadows by default (splats outside the view are not
selected); level switches pop per brick, without a blend; selection follows
the viewport's current camera only.

## Completion evidence

Require executable cooker tests, native resident raster scenes, uncull image
reference, cuts/near-plane/camera motion, dual viewport and off-camera shadows,
budget-pressure streaming without holes, and repeated CPU/GPU p50/p95/p99 plus
memory/quality comparisons. Record each completed slice in
[port status](PORTING_STATUS.md). Until those gates pass, the systems remain
planned—not implemented features exposed by placeholder controls.
