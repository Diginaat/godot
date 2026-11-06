---
type: Finding
title: Native MultiMesh GPU frustum culling
description: Experimental Forward+ per-viewport compute visibility retains original instance identity and conventional fallback.
tags: [rendering, geometry, gpu-culling, multimesh]
status: draft
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Implemented

Opt-in `rendering/geometry/multimesh_gpu_culling/enabled`, default off. Native
Forward+ compute tests transformed mesh-surface AABBs against six frustum planes,
emits source IDs and bounded indirect counts into per-viewport buffers, then
normal Godot materials rasterize those surfaces. Source transforms, color,
custom data, `INSTANCE_ID` and transform-history indices are not compacted.

Follow-up: the remap retains shader-language signed `int`. Visibility compaction
uses local ranks, block-prefix scan and ordered scatter rather than unordered
atomics; coplanar colored instances retain their source draw order. Vulkan/D3D12
fixtures assert ordered IDs and overlapping-instance image equality.

No production visibility readback or per-instance CPU loop. Visibility is
currently repeated per eligible surface. Each draw reserves enough ID slots for
every input; over-budget surfaces use direct submission. Buffers are cached per
viewport and released using RD lifetime/dependencies. Shadow passes are culled
only with the `multimesh_gpu_culling/shadows` opt-in (D011, 2026-11-06).
Unsupported deformation, custom transforms, transparency, separate shadow meshes,
indirect producer ownership, XR and double builds
retain their original path. The default specialized pipeline compiles out remap.
MultiMeshes whose world bounds lie inside every frustum plane skip culling and
keep the direct draw (2026-11-06), saving their three dispatches.
Surfaces with index LODs choose a LOD per visible instance (2026-11-06): up to
four levels between the CPU LOD of the bounds' nearest and farthest points, each
with its own ID range and indirect record. See D010 in
[decisions](../../knowledge/TECHNICAL_DECISIONS.md).
Jittered views (TAA, upscalers) cull against a frustum widened by the jitter,
which keeps culling exact (2026-11-06).

# Evidence

Production editor build passes. Vulkan and D3D12 on one GPU pass native-kernel
ID/count/range/history/boundary tests plus full-image culled/unculled equality at
65 and 100k potential instances per object. Two views and two transformed objects
share source resources; timestamp markers prove native dispatch in both views.
Viewport resize/resource replacement and standard PhysX/PT/DDGI smoke rerun pass.

No frame-time improvement claimed. HZB, cluster CLOD, virtual paging and distant
voxels are not implemented by this culler. Explicit subsequent requirements:
[virtual geometry design](../../knowledge/VIRTUAL_GEOMETRY.md).

Conditions, source files, exclusions and static-check limitations:
[architecture](../../knowledge/INTEGRATION_ARCHITECTURE.md),
[test record](../../knowledge/PERFORMANCE_BENCHMARKS.md).
