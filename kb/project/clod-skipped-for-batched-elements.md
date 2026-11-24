---
type: Finding
title: Cluster LOD skips repeat-batched render elements
description: Meshes shared by several nodes with same-shader materials batched into one repeated element since material parameter tables, and CLOD selection skipped repeated elements; fixed by not batching CLOD-eligible surfaces (street grid 14-37% faster).
tags: [geometry, clod, batching, rendering]
status: stable
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What happened

`hzb_bench.tscn` reports `HZB_BENCH_FAIL: ... did not exercise CLOD or
occlusion` in the street and corner views for every mode, with
`clod_surfaces: 0`. Its 225 dense objects share one `ArrayMesh` with
cluster data, each with its own `StandardMaterial3D` (the bench's comment
says that keeps them out of batching).

# Why

`_prepare_mesh_clod()` (`mesh_clod.cpp`) skips elements with
`element.repeat != 1`. With material parameter tables (D036), materials of
the same shader no longer split batches, so the 225 objects become repeated
elements and none is selected for cluster LOD. Not caused by the GPU path
(it rejects meshes with cluster data).

# Consequence

Any mesh reused by several nodes with same-shader materials got no cluster
LOD, and the bench's CLOD columns did not measure CLOD.

# Fix (2026-11-24, GPU-first plan G5)

`_fill_instance_data()` no longer batches a surface that can draw cluster
LOD in the opaque list: each instance stays its own element and gets its own
cut. The eligibility test is one helper, `_mesh_clod_surface_eligible()`
(instance, material, resident data, pass chain), plus
`_mesh_clod_frame_enabled()` (settings, view count, reflection probe,
mirror), used by both the list fill and `_prepare_mesh_clod()`. Shadow and
motion lists still batch. The selection binds its uniform set once per
surface, so many instances of one mesh cost a push constant and a dispatch
each.

Not done: one instanced draw with per-instance cuts. The commands would need
a per-command instance offset (`firstInstance` or a draw ID), and the D3D12
shader path zeroes the base instance (`DXIL_SPIRV_SYSVAL_TYPE_ZERO` in
`rendering_shader_container_d3d12.cpp`), so it would draw the wrong
instances there.

# Proof (Linux, RTX 3060, Vulkan)

`hzb_bench.tscn` (225 CLOD objects sharing one mesh, 14.9 M source
triangles): no `HZB_BENCH_FAIL`; street view draws 65 groups / 234,262
triangles, corner 168 / 255,230. GPU p50, before and after:

| View | off | two_pass+mm2 |
| --- | --- | --- |
| street | 5.571 to 4.036 ms | 4.409 to 3.095 ms |
| corner | 4.369 to 2.737 ms | 3.662 to 2.348 ms |
| aerial | 7.972 to 6.437 ms | 6.847 to 5.870 ms |

`clod`, `clod_raster`, `statistics`, `hzb`, `hzb_two_pass`,
`gpu_instances`, `multimesh_occlusion`, `culling`, `multimesh_lod`, path
tracer and DDGI smoke pass.

The fix first reached only 55 of the 225 objects: the CLOD viewport budget
was 65,536 groups and each object has about 1,190. The budget is now 2^20
groups and 16,384 draws (buffers grow to what a frame uses). With all
objects on CLOD (191 surfaces in the street view, 816,641 triangles):
`off` street 3.699, corner 2.662, aerial 4.302 ms.

Open: with CLOD drawn, CLOD HZB occlusion is slower than none here
(see [CLOD two-pass costs more than it saves](/project/clod-two-pass-zero-draws.md)).
