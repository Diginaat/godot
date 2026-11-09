# GPU-driven rendering: research notes

Sources used to choose the techniques in [roadmap.md](roadmap.md). They are
ideas and references, not code to copy. Status: 2026-11-10.

## Established techniques

| Technique | Primary sources | What it gives | Fit for this fork |
| --- | --- | --- | --- |
| Compute culling plus indirect draws | Haar and Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015 Advances; Wihlidal, "Optimizing the Graphics Pipeline with Compute", GDC 2016 | The CPU stops testing and submitting each object; the GPU writes the draw arguments | Already used for MultiMesh (D002, D003). The open gap is ordinary `MeshInstance3D` nodes |
| Two-phase HZB occlusion | Same SIGGRAPH 2015 talk; Karis, "Nanite: A Deep Dive", SIGGRAPH 2021 Advances; Bevy 0.16 two-phase occlusion culling | Objects hidden last frame are left out of the first depth pass and re-tested against this frame's pyramid | Implemented for CLOD (D023) and MultiMesh colour passes (D024); two-pass is a net loss in the city bench until phase-2 commands are compacted |
| Persistent GPU scene with dirty-range uploads | Frostbite and Unreal "GPU Scene" (Unreal 4.22+ mesh drawing refactor, GDC 2019 "Refactoring the Mesh Drawing Pipeline for Unreal Engine 4.22"); Bevy `GpuPreprocess` (instance input buffer, compute preprocessing) | Per-instance transforms and bounds live on the GPU; only changed instances are uploaded | Not in the fork. Godot uploads per-frame instance data only for visible draws (see [architecture.md](architecture.md)) |
| Cached draw commands | Unreal 4.22 "cached mesh draw commands" (same GDC 2019 talk) | Draw commands of static objects are built once and reused | Fits Godot's render-list design better than a full bindless rewrite. Candidate for Phase 4 |
| Multi-draw indirect with a count buffer | Vulkan `vkCmdDrawIndexedIndirectCount` (core in 1.2); D3D12 `ExecuteIndirect` with a count buffer | One API call per pipeline/material batch, GPU-decided count | RD has multi-draw (D009) but no count-buffer draw yet. Needs an RD extension on both drivers |
| Cluster LOD (virtual geometry) | Karis, SIGGRAPH 2021; meshoptimizer `clusterlod` | Screen-space error cuts, continuous LOD | Implemented (CLOD, streaming, HZB). Mesh shaders not required (D002) |
| Mesh shaders | `VK_EXT_mesh_shader`, D3D12 mesh shaders; NVIDIA "Introduction to Turing Mesh Shaders" (Kubisch, 2018) | Per-meshlet culling inside the pipeline | Optional fast path only. Not supported by every GPU or by Metal through RD today; never a requirement |
| GPU light binning | Godot's own clustered builder (raster of light volumes into a cluster buffer); Olsson et al., "Clustered Deferred and Forward Shading", HPG 2012 | Light lists built on the GPU | Godot Forward+ already bins on the GPU. Measure the CPU light setup before changing anything |
| GPU BVH build (LBVH) | Karras, "Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees", HPG 2012 | Fast rebuilds of dynamic hierarchies | Only with a consumer. Ray tracing uses driver acceleration structures (TLAS/BLAS) already |

## Engines checked

- **Godot upstream.** The lead developer's GPU-driven proposal
  (godot-proposals #6109, linked design gist) describes a separate deferred,
  bindless, fully GPU-driven path. It is not merged. This fork extends the
  existing Forward+ path instead (D001), so upstream merges stay small.
  Revisit if upstream starts landing that design.
- **Bevy.** Has GPU preprocessing of a persistent instance buffer, GPU
  frustum culling, multi-draw indirect, bindless materials and two-phase
  occlusion. Closest public reference for "GPU culling on an existing
  forward renderer"; it needed bindless materials to make multi-draw batches
  large.
- **Unreal Engine.** GPU Scene plus cached mesh draw commands (4.22) came
  before Nanite. The cached commands removed most CPU submission cost
  without a bindless rewrite.
- **CRYENGINE 5.7** (local source, ideas only, licence): CPU-side
  multi-threaded culling jobs and per-pass render lists, software occlusion.

## Lessons that change the plan

1. Godot's main cull is already a multi-threaded linear scan of a packed
   array. Moving it to the GPU only helps if render-list building and
   submission also leave the CPU; a GPU cull whose results return to the
   CPU needs a readback, which the rules forbid.
2. Multi-draw batches are only large when draws share a pipeline and a
   material uniform set. Godot binds a material uniform set per material,
   so unique-material scenes stay one draw per material without bindless
   material data. Measure `unique_10k` against `shared_20k` before deciding.
3. GPU work can rise while CPU work falls; every phase reports the whole
   frame (wall, render CPU, GPU p50/p95/p99).
