---
type: Finding
title: GPU occlusion culling for CPU render list meshes
description: HZB-tested indirect draws for every opaque CPU-list mesh, so occlusion covers all meshes, not only the GPU paths; two-pass keeps hidden meshes out of the depth pre-pass too.
tags: [geometry, occlusion, hzb, gpu, rendering]
status: stable
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What

`rendering/geometry/mesh_occlusion/enabled` (default on). Before it, HZB
occlusion covered only CLOD groups, GPU-culled MultiMesh and GPU-path
MeshInstance3D. Meshes on the CPU render list (lightmapped, VoxelGI,
skinned, blend shapes, fade, visibility range) were never occlusion-culled.

# How

After the depth pre-pass and HZB build (`_setup_mesh_occlusion()`,
`_occlude_meshes()` in `mesh_hiz.cpp`, kernel `mesh_occlusion.glsl`):
one record per opaque list element (world box = union of the instances the
element draws, index or vertex count, instance count). The kernel uses the
shared `hzb_box_occluded()` with the GPU-instance rounding bound and writes
one 5-word indirect command per element. The opaque colour pass draws a
tested element with `draw_list_draw_indirect()` on that command (0 instances
when hidden); untested elements keep the direct draw. Depth pre-pass and
shadows unchanged: saves shading, not depth. `meshes_occluded` statistic.
`mesh_occlusion.ready` is reset every frame, so a frame that skips the setup
never reuses old commands.

## Two-pass depth (2026-11-24, GPU-first plan G3)

`rendering/geometry/mesh_occlusion/two_pass` (default on). History: one word
per GPU instance slot (`gpu_slot`, stable for the instance's life), 1 if
occluded when last tested, in `MeshOcclusionBuffers::history` per viewport;
each element lists its instances' slots. Before the pre-pass,
`_split_mesh_occlusion()` (`MODE_SPLIT`) writes per element a phase-1 depth
command with its instances only if one was visible last time; the pre-pass
draws tested elements through it. The occlusion test (moved before the
phase-2 pass) also writes the phase-2 command (visible now, not in phase 1,
read from the phase-1 command, not the history: an instance's surfaces are
separate elements sharing a slot) and the history. The phase-2 depth pass
(`depth_phase2_only`) draws them with CLOD and MultiMesh phase 2. Depth
commands carry the depth pass's own index count (`PASS_MODE_DEPTH` draws the
shadow mesh). New history slots read as visible. `meshes_depth_deferred`
statistic.

Proof: `mesh_occlusion.tscn` (permanent fixture): 20 hidden meshes left out
of colour and phase-1 depth; static views and 30 alternating front/back
frames equal the reference; one-pass and off match. The scene has SSAO
because the colour pass writes depth itself (GREATER_OR_EQUAL): without a
depth reader a missing phase-2 draw does not show. Negative control (phase-2
instance count forced to 0): 30 of 30 alternating frames differ.
`multimesh_occlusion.tscn` got SSAO for the same reason.

Speed: `hzb_bench` with 170 of its 225 dense objects (about 66 k triangles
each) on the CPU list (CLOD budget exceeded, before the budget was raised):
GPU p50 street 3.095 to 1.687 ms, corner 2.324 to 1.546, aerial 6.073 to
5.375; depth p50 about halved. With every object on CLOD the bench has no
tested CPU-list mesh left. The valley map has none either.

## Two-pass depth for the GPU path (2026-11-24)

`rendering/geometry/gpu_instances/occlusion_two_pass` (default on), in
`gpu_instance_cull.glsl` (`VIEW_FLAG_TWO_PASS`, bindings 14-17). The camera
run reads a per-table-slot history (`GPUInstanceScenario::history_buffer`,
the same `gpu_slot` key as the CPU list), records per member whether it went
to phase 1, and appends members whose instance was visible when last tested
to phase-1 commands and IDs (layout of `commands_buffer`/`ids_buffer`); the
depth pre-pass draws those. `MODE_OCCLUDE` (now before the phase-2 pass)
writes the history and appends visible members not in phase 1 to the phase-2
lists; the phase-2 depth pass draws GPU buckets from them with its own
render-pass uniform set. Reading the phase-1 flag instead of the history
avoids the race between an instance's surfaces. Without two-pass the
bindings get stand-in buffers the shader does not touch.
`gpu_instances_depth_deferred` statistic.

Proof: `mesh_occlusion.tscn` runs every check for the GPU path too (spheres
without a visibility range): 20 occluded and deferred, static and 30
alternating frames equal the reference; negative control (phase-2 append
removed): 30 of 30 alternating frames differ. All large-world fixtures pass.

Speed: neutral. `hzb_bench` (64 buildings on the GPU path) within 1-3%;
`instance_bench` (up to 100k nodes, little occlusion) within noise: a first
off-then-on run showed the 100k cases 1.2 ms slower, the reversed on-then-off
run showed no difference (run order, not the feature). Gains need GPU path
meshes with expensive depth behind occluders.

Not tested: GPU path, CLOD and MultiMesh/particle elements (own paths),
points, shaders using `VERTEX`/`POSITION`/custom transforms (box may not
bound them), XR, wireframe and overdraw debug modes; HZB preconditions
(depth pre-pass, no MSAA/compositor/render region, no depth-writing or
stencil opaque shader) apply.

# Proof (2026-11-24, Linux, RTX 3060, Vulkan, .NET editor)

Scratch fixture: wall, 20 visibility-range spheres behind it, 5 in front,
3 over the edge. Setting off: 0 occluded; on: exactly 20; images
byte-identical; camera behind the wall: the 5 front spheres occluded.
Large-world `hzb`, `hzb_two_pass`, `multimesh_occlusion`, `clod`,
`culling`, `statistics`, `gpu_instances` (279 HZB, 0 image differences)
pass. `clod_raster` asserts that only CLOD builds the HZB; it now pins
mesh occlusion off, like the other occlusion settings (D035). Path tracer
and DDGI smoke pass; the PhysX example project was not on this machine.

## Local batches and the frame-time number (2026-11-27)

`mesh_occlusion_bench.tscn` (roadmap G9): 12 x 12 walled courtyards, each
with 9 spheres of about 4 k triangles and a subsurface scattering material.
SSS keeps them on the CPU list; a visibility range no longer does, since
the GPU instance path takes those. The walls hide the courtyards from the
street; from above they are open.

The first run found nothing tested: `meshes_occluded` 0. The CPU list
draws equal consecutive elements as one instanced draw, and the HZB test
uses one box around the whole batch. Batches only break at the 16 sort
depth layers, so a batch spans the town's width at one distance, and its
box covers the open street. With mesh occlusion on,
`_fill_instance_data()` now ends a batch (`p_local_batches`) before its
merged box gets longer than 8 times its first member's. Equal keys come in
cull (BVH) order, so the batches stay local. The depth pre-pass already
keeps hidden pixels from shading: the saving is vertex and raster work in
the depth and colour passes.

RTX 5070, Vulkan, 1920x1080, p50 ms, mesh occlusion off / on:

| View | GPU frame | Depth pass | Opaque pass | Render CPU |
| --- | --- | --- | --- | --- |
| street | 1.043 / 0.570 | 0.413 / 0.182 | 0.462 / 0.217 | 0.198 / 0.251 |
| aerial (all visible) | 1.078 / 1.095 | 0.441 / 0.454 | 0.466 / 0.467 | 0.198 / 0.253 |

D3D12: street GPU 1.017 / 0.546, aerial 1.042 / 1.060. In the street
view 199 batches are occluded. The cost of smaller batches
is about 0.055 ms of render CPU for about 200 more draws, paid in every
view. Images are identical with occlusion off and on (the bench fails
otherwise). The depth and opaque stage timestamps are now taken for every
camera render, not only with CLOD or MultiMesh culling.
