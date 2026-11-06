---
type: Finding
title: Native cluster hierarchy cooker and checked resource
description: Index-only group hierarchy built with existing meshoptimizer, with border locks and complete reference cuts.
tags: [geometry, clod, clusters, resources]
status: draft
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# Implemented part

Attachment follow-up: ArrayMesh stores optional cluster_data after source
surfaces, with convenience generation and conservative geometry/attribute
invalidation. SHA-256 over encoded decoded source arrays checks provenance at
attachment/load (not per frame); material and name edits do not change it.
Shared cache changes recheck owners and detach stale sources. Old unsigned
caches require regeneration before attachment. Native Resource and rendering
storage now share a validator without a rendering-to-scene dependency.
Production build, Vulkan/D3D12 lifecycle/serialization checks, focused XML help
validation and standard PhysX/PT/DDGI smoke pass; GPU raster still absent.

Native MeshClusterData Resource and a meshoptimizer-module generator construct
per-surface hierarchical simplification groups. Existing MIT meshoptimizer 1.2
is sufficient; no external cooker or dependency update. Original vertex buffers
and attributes remain intact; derived indices refer to source vertices.

The first structure is a group tree. Child groups precede parents; every parent
has a complete replacement representation, conservative child-enclosing bounds
and accumulated error. Border locks preserve interfaces during simplification.
The CPU reference selector traverses roots exclusively, not independent projected
parent/self comparisons: floating bounds can break numerical monotonicity even
when object-space errors are monotonic. Double intermediates and hypot avoid
finite-float overflow removing subtrees.

Checked serialization rejects invalid index/record ranges, cycles, inconsistent
parents, missing roots, orphan ranges and invalid bounds/errors. Unsupported
deformation/custom channels return errors without changing the source resource.

Initial curved-grid test passes: source attributes unchanged, complete
non-overlapping cuts, unchanged topology boundaries, 13,824 source indices to
3,456 root-cut indices, resource save/load and corrupt-data rejection. Extreme
finite projection inputs pass after root-traversal/double-arithmetic hardening.
Production build and headless/Vulkan/D3D12 fixture runs pass. This is not yet GPU
CLOD raster or geometry streaming.

Sources and conditions: [virtual geometry](../../knowledge/VIRTUAL_GEOMETRY.md),
[validation](../../knowledge/CLUSTER_COOKER_TESTING.md).
