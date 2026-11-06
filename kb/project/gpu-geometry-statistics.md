---
type: Finding
title: Native GPU geometry statistics
description: Per-viewport counters written by the MultiMesh culling, CLOD selection and HZB kernels, read back asynchronously only while polled.
tags: [rendering, profiling, gpu-culling, clod, hzb]
status: draft
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T00:00:00+01:00 }
---

# Implemented

`RenderingServer.viewport_get_gpu_geometry_statistics(viewport)` returns the
latest counters for a viewport's main view: MultiMesh surfaces culled or fully
visible, instances tested/visible, CLOD surfaces and groups tested/selected/
visible, visible CLOD triangles, and HZB-rejected groups and triangles.

The production kernels are compiled with `GEOMETRY_STATISTICS` and add into an
24-word per-viewport buffer (shadow-pass MultiMesh culling from word 12) (one atomic per MultiMesh surface scan, per CLOD
workgroup and per selected or rejected group). Test fixtures compile the shader
sources without the define. The buffer is cleared and copied with
`buffer_get_data_async` only during the 120 frames after the last call, so
there is no project setting and no readback unless someone polls. Results lag a
few frames; the dictionary carries the frame they describe. The callback
resolves the viewport's buffers by ObjectID, so a freed viewport is skipped.

Code: `servers/rendering/renderer_rd/forward_clustered/geometry_statistics.cpp`;
bindings in `multimesh_culling.cpp`, `mesh_clod.cpp`, `mesh_hiz.cpp`.

# Evidence

`misc/large_world_test_project/statistics.tscn` (in `devtools/test/large_world.ps1`)
checks exact values on Vulkan and D3D12: 1000 tested / 700 visible instances,
one fully visible 50-instance MultiMesh, 2 × 76 tested groups, 2 × 4224 visible
triangles at 0 px (the full source mesh), fewer at 64 px, HZB rejection of a
hidden sphere only with occlusion on, and zero counts with the paths disabled.

The valley benchmark prints the counters per view. They showed that CLOD
draws 17k–95k triangles while the frame submits 3–5 M primitives, mostly
full-detail MultiMesh vegetation; see `knowledge/PERFORMANCE_BENCHMARKS.md`.
