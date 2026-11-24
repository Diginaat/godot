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

No frame-time number yet: needs a bench scene with expensive CPU-list
materials behind occluders.
