---
type: Finding
title: Cluster LOD skips repeat-batched render elements
description: Meshes shared by several nodes with same-shader materials batch into one repeated element since material parameter tables, and CLOD selection skips repeated elements, so hzb_bench draws no CLOD.
tags: [geometry, clod, batching, rendering]
status: open
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

Any mesh reused by several nodes with same-shader materials gets no cluster
LOD. GPU-first plan G5 (per-instance cluster LOD inside instanced draws)
fixes it; until then the bench's CLOD columns do not measure CLOD.
