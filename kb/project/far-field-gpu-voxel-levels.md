---
type: Finding
title: GPU voxel levels for the far field
description: Voxel levels and per-brick level selection, run in a compute pass that fills an indirect MultiMesh.
tags: [geometry, far-field, voxels, compute]
status: experimental
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T00:00:00Z }
---

# What it does

`VoxelProxyBuilder.build_field(bounds, resolution, brick_resolution)` turns
meshes into a `VoxelFieldData`: voxel levels from `resolution` down to one
voxel per brick, with each brick listing its voxels per level. `VoxelField3D`
draws the data as camera-facing discs. Every frame the GPU picks one level per
brick, so splats stay about `voxel_pixels` wide on screen: fine voxels near the
camera, coarse ones far away. This answers user feedback that the static
proxies' voxels were too big near the switch distance.

# How it works

* Voxel levels ported from a reference engine: 2x2x2 merges that
  sum area, and selection of the coarsest level whose voxel is at most N pixels
  wide at the brick's nearest point.
* One workgroup per brick: sphere frustum test, level pick, per-splat frustum
  test, shared-memory compaction, and one global atomic per 64 voxels. It
  writes the splats into an indirect MultiMesh, the same pattern as the GPU
  grass.
* Each voxel takes 16 bytes. The instance buffer grows with the GPU's count
  (read back asynchronously). Fields outside their visibility range skip the
  dispatch.
* The reference engine's atomic visibility buffer was not ported: Godot's renderer draws the
  splats with its materials, fog and depth.

# Evidence

The `voxel_field` fixture passes on Vulkan and D3D12. Data statistics are
exact on a flat square and on quadrants. The GPU selects exactly the same
splats as the CPU mirror at 300, 1000, 2000 and 4000 m and with an orthogonal
camera. It also checks frustum culling, the capacity cap, an empty field,
the auto-capacity bound, coverage and colour against the source, and a free
with work in flight.

In the valley, against the source meshes, the image error falls from 5.13 to
3.69 (mean linear difference x 1000). The first cost figures (-0.05, +0.71
and +0.16 ms GPU p50 against the static proxies, D018) came from the
viewport's render time and missed the selection, which runs before the
viewport ([the mistake](/mistakes/viewport-time-misses-pre-viewport-work.md)).
Whole frame, the field cost 4.9 ms more than the static proxies, 3.1 ms of
it from one GPU timestamp per field: a timestamp orders the render graph, so
149 fields ran their clears, selections and copies one after another. Without
the per-field timestamp (2026-11-08) the field costs 2.0 ms more on Vulkan
and 1.6 ms on D3D12: 0.5 ms selection, the rest drawing about 27,000
instanced two-triangle quads in the depth and opaque passes.

# Limits

* No shadows by default.
* A brick's level switch pops, without a blend.
* Selection follows only the viewport's current camera.
* The valley build doubles to 3.8 s.
* Tiny instances draw slowly: about 30 ns per splat per pass. Drawing the
  splats from one buffer without instancing (vertex pulling, six vertices
  per splat) is the next step for its cost.
