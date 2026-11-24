---
type: Finding
title: Native resident CLOD raster path
description: Checked ArrayMesh hierarchies drive viewport-owned GPU commands through native Forward+ vertex and material pipelines.
tags: [geometry, clod, gpu, materials, rendering]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Ownership

ArrayMesh cluster attachment forwards typed hierarchy dictionaries through
RenderingServer to RD MeshStorage. Renderer storage shares cache validation with
the Resource without depending on scene code. Each surface owns a resident
32-bit alternate index buffer and 48-byte group records; existing vertex and
material buffers remain authoritative. RD handles submitted-reader retirement.
Surface destruction and vertex/attribute/skin/index updates clear resident data.

Forward+ owns per-viewport selection inputs and indirect commands. Opt-in
`rendering/geometry/cluster_lod/enabled` runs the tested ancestor-veto selector,
then native depth/opaque submission binds the alternate index array while using
existing vertex arrays, shader pipelines, materials and instance transforms.
Shadow, RT and other passes retain source geometry. No CPU selection/readback.

# Conservative first scope

Ordinary static, unbatched MeshInstance3D first. Excludes deformation, alpha,
stencil, custom transforms, multiple material passes/overlays,
selected source LODs, mirror/reflection views, jitter, XR and double
builds. Unsupported work and exceeded budgets keep conventional draws.

Resident limits: 4096 groups per surface; 64 MiB group/index bytes per mesh.
Viewport limits: 1,048,576 groups (20 MiB commands) and 16384 draws (2.25 MiB input); buffers grow only to what a frame uses. Until 2026-11-24 they were 65536 groups and 4096 draws, which `hzb_bench` (225 objects of about 1,190 groups) exceeded once shared meshes got CLOD: only 55 objects drew cluster LOD.
Scratch remains cached until viewport resize/destruction. Error scale uses a
conservative Frobenius model-basis bound (over-refines even identity by sqrt(3)).
Group records are submitted per surface with one multi-draw indirect call when
RD reports `SUPPORTS_MULTI_DRAW_INDIRECT`, else one call per record (2026-11-06).
Measured in the valley benchmark: CLOD render-thread overhead fell from
+0.17–0.31 ms to +0.02–0.06 ms p50; GPU time unchanged.

# Verification

Initial production build and Vulkan/D3D12 five-part runners pass. Native test
checks exact source/fine pixels with StandardMaterial3D and ShaderMaterial,
multiple surfaces and three views, reflected nonuniform transforms, viewport
resize, visibly changed coarse raster, return to fine, geometry/RD index
invalidation, recook, custom vertex fallback and destruction. A top-down floor
crop contains a measurable cast shadow and remains byte-identical at coarse
camera CLOD; conventional shadow draw counts stay nonzero and unchanged.
Material-next-pass fallback also passes after conservatively excluding duplicate
passes over each surface. Final standard PhysX/PT/DDGI smoke rerun passes.
No HZB, streaming, paging, far voxels or vegetation is claimed.

Scene-import follow-up supports separately optimized shadow meshes: source camera
depth uses original source vertices for hierarchy indices; actual shadows retain
their own independently indexed mesh. A reversed shadow-ID raster fixture passes
on both backends, including unchanged measurable cast-shadow pixels at coarse CLOD.

# Per-instance control

`MeshInstance3D.cluster_lod` (Project Default, Enabled, Disabled) and
`RenderingServer.INSTANCE_FLAG_USE_CLUSTER_LOD` (default on; Disabled sets it
off; `_prepare_mesh_clod()` skips instances without it). Enabled cooks an
ArrayMesh without cluster data on enter tree (main thread, stored on the
mesh, once per shared mesh). `rendering/geometry/cluster_lod/mesh_instance_default`
makes Project Default behave as Enabled; off, Project Default only draws
already-cooked meshes with CLOD (the old behavior). RenderingServer cannot
cook. Multi-select edits the property on several nodes. Test: a scratch
scene with each mode and RS flag, checked in the Cluster LOD debug view
(10 checks pass, Linux Vulkan, .NET editor).

# Debug view

`Viewport.DEBUG_DRAW_CLUSTER_LOD` (editor: Display Advanced > Cluster LOD)
colors each selected group by a hash of group id and instance; geometry not
drawn through CLOD is gray. The color pass then draws one indirect call per
group record (no multi-draw) and pushes the group id in
`PushConstant::cluster_lod_group`; the debug material's `debug_cluster_lod`
render mode makes the scene shader read it. Debug only: up to 65536 calls.
At the default 1 px error a smooth sphere often selects a single root group
(one color); `error_pixels = 0` shows the finest groups. A mesh shared by
several MeshInstance3D nodes is batched (`repeat > 1`) and stays gray.

Source/evidence: `mesh_clod.cpp`, RD `mesh_storage.cpp`,
`misc/large_world_test_project/clod_raster.gd`,
[commands](../../knowledge/CLUSTER_COOKER_TESTING.md).
