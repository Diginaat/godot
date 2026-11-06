---
type: Finding
title: Residency-aware complete CLOD cuts
description: CPU coverage snapshots and a GPU selector handle missing representations without overlapping cuts or holes.
tags: [geometry, clod, residency, gpu]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Implemented protocol, not a page pool

MeshClusterData.get_residency_state computes resident/covered flags bottom-up.
Missing intermediate representations can remain covered by fine descendants.
Every root must be covered; otherwise the whole surface needs source fallback.

The GPU MODE_RESIDENCY variant refines missing intermediate representations,
retains complete coarse parents on misses and accumulates requested/used bits
across views. An incomplete header zeros all commands and sets a fallback word.
Future native submission must check CPU completeness before using this variant.
Current native raster still uses full residency; no placeholder setting added.

A validated two-root forest of cooked grids checks 24 changing masks and two
thresholds: recursive CPU coverage, GPU root-oracle cuts, exactly one selected
ancestor per leaf, full/root/leaf/random/partial/empty residency, shared feedback,
bounded commands and guards. Production build, Vulkan/D3D12 eight-part suites
and standard smoke pass. Page allocation, eviction, asynchronous feedback and
disk streaming remain unimplemented; source buffers remain resident.
