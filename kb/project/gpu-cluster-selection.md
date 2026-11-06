---
type: Finding
title: GPU hierarchy cuts with ancestor veto
description: Source-index indirect commands selected by projected error with complete cuts and no local traversal stack.
tags: [geometry, clod, gpu, rendering]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Kernel

`mesh_clod_select.glsl` emits one five-word indexed command per hierarchy group.
Each group is selected iff it meets the error threshold (or is a leaf) and every
ancestor refines. Equivalent to exclusive root traversal, without a bounded
local stack. Parents precede no descendants in the validated upward chain, so
cycles are impossible. Cost is group count times ancestor depth, not yet timed.

Perspective uses nearest camera-forward AABB depth, orthographic uses constant
projection scale. Model error scaling is supplied by the caller. Pulled-back
frustum planes cull selected regions; overflow/NaN comparisons refine instead of
silently removing geometry. Source vertex/material buffers are unchanged.

# Proof and boundary

Real Vulkan and D3D12 tests in `misc/large_world_test_project/clod.gd` compare
commands with an independent root-walk oracle: perspective/orthographic,
multiple thresholds/views, partial/full rejection, changed thresholds,
visibility recovery and reversed repeat submission. Every command word and
leading/trailing capacity guards are checked. Both expanded focused runners
pass; production build and standard PhysX/PT/DDGI smoke pass.

This verifies a compute kernel, not native scene attachment-to-raster wiring,
residency, paging, import automation or a performance improvement.
