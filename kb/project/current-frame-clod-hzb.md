---
type: Finding
title: Current-frame conservative CLOD HZB
description: A complete depth prepass supplies a padded reverse-Z farthest-depth pyramid for opaque-only group rejection.
tags: [geometry, clod, hzb, occlusion, gpu]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Baseline and ownership

Opt-in `rendering/geometry/cluster_lod/occlusion_enabled` runs only for already
eligible resident CLOD and a completed current-frame depth prepass. It does not
save depth work or use previous visibility. Every new frame rebuilds group
commands, renders complete selected depth, rebuilds HZB, then rejects opaque
commands; moving walls/cameras do not need temporal recovery. Shadows and source
geometry remain conventional.

Per-viewport R32F mip storage is power-of-two padded with clear depth zero.
Reverse-Z occlusion needs the farthest (minimum) covered depth, not the existing
SSR nearest (maximum) pyramid. Padding preserves all NPOT edge pixels; holes and
padding cannot become occluders. The full chain has a conservative 64 MiB budget
using twice base-image bytes as its upper bound. RD tracks render/compute/indirect
dependencies and submitted-reader retirement. Source depth is never replaced.

GPU projection keeps clipped/near-plane/screen-edge uncertainty visible. Bounds
receive an abs(P)*abs(V)*abs(M) float-operation error interval, growing with large
world-coordinate cancellation; non-finite or uncertain intervals retain groups.
Expanded pixel rectangles sample every intersected tile of a selected mip and
reject only when the nearest bound is strictly behind all sampled farthest depth.

MSAA, custom compositors, partial render regions, unsupported R32F storage,
missing depth prepasses and opaque custom fragment-depth/stencil shaders keep
unoccluded CLOD. The fragment-depth guard scans occluders too, not just targets.
No production CPU visibility readback and no speedup claim.

# Proof state

Production build and both final seven-part GPU-backend matrices pass.
GPU tests compare every padded mip to a CPU min-depth oracle, check bounded
command edits, actual hidden/front/touching/clip/edge cases and one-pixel-hole
recovery. Native raster checks source/fine pixels across three viewports,
moving occluders, coarse/fine transitions, preserved shadows, MSAA and custom
fragment-depth fallback, resize, source mutation and destruction. Numerical
uncertainty checks, a 100000-unit source/camera translation and final standard
PhysX/PT/DDGI smoke rerun pass. These are correctness cases, not performance measurements.

One editor `--import` shutdown failed with null EditorNode singleton and
0xC0000005 while the new scripts were first scanned. Immediate isolated retry,
five repeated editor imports and both complete subsequent suites passed. Root
cause remains unknown; do not claim it fixed or infer a HZB failure from that
single shutdown. The test runner rejected it rather than silently retrying.

Sources: `mesh_hiz.cpp`, `mesh_hiz_reduce.glsl`, `mesh_hiz_occlude.glsl`,
`hzb.gd`, `clod_raster.gd`, `devtools/test/large_world.ps1`.
