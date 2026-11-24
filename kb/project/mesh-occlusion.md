---
type: Finding
title: GPU occlusion culling for CPU render list meshes
description: HZB-tested indirect draws for every opaque CPU-list mesh, so occlusion covers all meshes, not only the GPU paths.
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
