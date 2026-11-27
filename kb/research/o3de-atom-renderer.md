---
type: Reference
title: O3DE Atom renderer - what this fork can learn
description: Atom's frame graph (transient aliasing, load/store elision, async queues), depth-binned tile light culling, auto-instancing, job pipelining and shader variant fallback, compared with Godot Forward+ and this fork; ideas, nothing measured.
tags: [research, renderer, o3de, frame-graph, light-culling, instancing, shader-variants]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-26T00:00:00Z }
sources:
  - id: fgc
    resource: https://github.com/o3de/o3de/blob/development/Gems/Atom/RHI/Code/Source/RHI/FrameGraphCompiler.cpp
    title: O3DE Atom RHI FrameGraphCompiler.cpp (development branch, Apache-2.0 OR MIT)
  - id: lc
    resource: https://github.com/o3de/o3de/tree/development/Gems/Atom/Feature/Common/Assets/Shaders/LightCulling
    title: O3DE Atom LightCullingTilePrepare / LightCulling / LightCullingRemap .azsl
  - id: mfp
    resource: https://github.com/o3de/o3de/blob/development/Gems/Atom/Feature/Common/Code/Source/Mesh/MeshFeatureProcessor.cpp
    title: O3DE Atom MeshFeatureProcessor.cpp and MeshInstanceManager.{h,cpp}
  - id: frame
    resource: https://docs.o3de.org/docs/atom-guide/dev-guide/frame-rendering/
    title: O3DE docs, Atom frame rendering (feature processors, draw packets, views, draw lists)
  - id: variants
    resource: https://docs.o3de.org/docs/atom-guide/dev-guide/shaders/azsl/shader-variants-fallback-key/
    title: O3DE docs, shader variant fallback key and root variant
  - id: moc
    resource: https://www.docs.o3de.org/docs/api/gems/atom/class_masked_occlusion_culling
    title: O3DE Atom API, MaskedOcclusionCulling (Intel MOC)
---

# Summary

Read 2026-11-26 from O3DE source (development branch) and docs. Parts of
long files were only read as summaries; those points are marked
(unverified). No code taken; ideas only. Nothing measured.

Overall: this fork's GPU-driven geometry (persistent instance table, GPU
culling, HZB two-pass, material tables, CLOD) is ahead of Atom, which
culls and instances on the CPU. Atom's useful lessons are in frame
scheduling, light culling and CPU job structure.

## 1. Frame graph: transient memory aliasing[^fgc]

* Passes declare attachments (Input, Output, InputOutput). Per frame the
  `FrameGraphCompiler` runs: queue graph, then transient attachments, then
  resource views, then per-scope compile.
* Each transient attachment gets an Activate at its first scope and a
  Deactivate at its last. Commands are sorted (deactivate before
  activate inside a scope), so later attachments reuse the memory of
  dead ones from a pool that resets every frame.
* Memory never aliases across queues. In an async-compute interval the
  queue with the most transient attachments gets aliasing; the others
  have their lifetimes stretched to the interval bounds.
* Load/store elision: first use becomes `DontCare` load (unless it
  clears), last use becomes `DontCare` store.
* Optional two-pass allocation: first pass only measures heap size.

Godot RD has no frame graph: render buffers are persistent per viewport
and every pass names its own load/store actions. **Idea**: a small
transient pool for per-frame intermediates (SSR, SSAO, SSIL, bloom,
DDGI apply, HZB, VRS) with lifetimes from pass order could cut VRAM,
mostly at 4K and with several viewports. Measure VRAM per feature first;
it is only worth it if intermediates are a large share.

## 2. Tile light culling with per-tile depth bins[^lc]

Three compute passes:

1. **TilePrepare**: one thread per pixel (tiles usually 16x16). Reduces
   the tile's opaque depth min/max, splits that range into 32 bins and
   builds a 32-bit occupancy mask from the actual depth samples (MSAA
   2x/4x aware, 8x reuses 4x). Transparent geometry has its own min/max
   depth textures and fills every bin between them (conservative). Sky
   pixels take the farthest transparent depth.
2. **LightCulling**: one group per tile; coarse bounding-volume tests per
   type (decals, point, spot, disk, capsule, quad). Each entry is a
   16-bit light index plus a 16-bit bin mask (unverified widths). Decals
   are bitonic-sorted by sort key per tile. Fine-grained tests are a TODO.
3. **Remap**: per tile and per bin, a compact list ending in a marker;
   the forward shader finds its pixel's bin and walks that list. Per-bin
   overflow sets bit 31 for a heatmap.

Godot Forward+ (`ClusterBuilderRD`) rasterizes light and decal shapes
into 32 px tiles and a z mask of fixed view-space slices, baked in
`_pre_opaque_render()` after the depth pre-pass. Its xy test is tighter
(real cone and box shapes), but z slices ignore where geometry is.
**Idea**: when the depth pre-pass ran, AND each tile's element z mask
with a per-tile occupancy mask from the depth buffer, so a light behind
the first surface or in an empty depth gap is skipped. Cheap (one compute
over depth). Worth testing on many-light scenes where tiles span large
depth ranges (foliage edges, interiors with windows). Transparent pixels
need the conservative range, as Atom does.

## 3. Automatic instancing on the CPU[^mfp]

* `r_meshInstancingEnabled` groups objects by model, LOD, mesh, material
  and sort key (key fields unverified). One draw packet per group and
  view; the group's first index goes in a root constant and the count in
  the instance count.
* After culling: visible objects are scattered into per-group buckets in
  batches (`r_meshInstancingBucketSortScatterBatchSize`) with a lock-free
  atomic slot into arrays sized for the worst case; then one sort task
  per bucket (the code notes overfull buckets can unbalance the tasks).
* Draw packets are only rebuilt on shader or material changes; other
  changes update the object's SRG.[^frame]

This fork's GPU path already does the same on the GPU (buckets, indirect
draws). Nothing to take, except the note on unbalanced buckets for the
CPU render list sort.

## 4. Job pipelining without a global sync[^mfp]

With instancing off, each range's culling-update job depends only on the
same range's init job, not on all init jobs; the code comment says this
beats one big sync. **Idea** for the scene runtime
([docs/scene-runtime/MEMORY.md](../../docs/scene-runtime/MEMORY.md)) and
the threaded cull: chain per-range stages instead of a barrier between
whole phases where data is range-local.

## 5. Shader variant fallback[^variants]

* Options are bits in a 128-bit fallback key, stored in one SRG. The root
  variant keeps every option as a runtime branch and can draw anything.
* Baked variants are chosen from a variant tree as the best fit for a
  key; options the found variant leaves open come from the fallback key
  at runtime. Partial specialization, not all-or-nothing.

Godot already has an ubershader with specialization constants while the
specialized pipeline compiles, and this fork compiles scene variants per
feature group ([docs/shader-permutations/memory.md](../../docs/shader-permutations/memory.md)).
**Idea**, low priority: a partially specialized fallback (bake the
expensive options, leave rare ones dynamic) could lower the number of
pipelines a material needs. Only if pipeline counts become a problem
again.

## 6. Occlusion

Atom uses Intel Masked Occlusion Culling on the CPU plus artist
occluder planes.[^moc] Godot has CPU occluders (`OccluderInstance3D`,
Embree raster) and this fork has GPU HZB two-pass. Nothing to take.

# Not read

`LightCullingShared.azsli` (tile size, bin count, max lights per bin),
the forward shader's light walk, the RHI frame scheduler page, command
list partitioning for multithreaded recording, pipeline state caching.
