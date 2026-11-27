---
type: Reference
title: Mesh shaders (state in late 2026)
description: API limits (D3D12, VK_EXT_mesh_shader, Metal), vendor meshlet sizing, culling efficiency, platform coverage, and the state of mesh shaders in Godot's RenderingDevice; what it means for D002.
tags: [research, renderer, mesh-shaders, meshlets, gpu-driven]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-26T00:00:00Z }
sources:
  - id: d3d12spec
    resource: https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html
    title: Microsoft, D3D12 Mesh Shader spec (v0.86)
  - id: vkext
    resource: https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_mesh_shader.html
    title: Khronos, VK_EXT_mesh_shader proposal
  - id: zeux
    resource: https://zeux.io/2023/01/16/meshlet-size-tradeoffs/
    title: Kapoulkine, Meshlet size tradeoffs (blog, 2023-01-16)
  - id: gpuopen
    resource: https://gpuopen.com/learn/mesh_shaders/mesh_shaders-optimization_and_best_practices/
    title: AMD GPUOpen, Mesh shaders - optimization and best practices (2024)
  - id: nvapi
    resource: https://developer.nvidia.com/blog/advanced-api-performance-mesh-shaders/
    title: NVIDIA, Advanced API Performance - Mesh Shaders
  - id: xbox
    resource: https://devdocs.xbox.com/build/core-features/graphics/newfeatures/ms/ms-why
    title: Microsoft GDK docs, why mesh shaders
  - id: tellusim
    resource: https://tellusim.com/compute-raster/
    title: Tellusim, compute rasterization vs mesh shaders vs MDI (2021)
  - id: metal
    resource: https://developer.apple.com/videos/play/wwdc2022/10162
    title: Apple WWDC22, Transform your geometry with Metal mesh shaders
  - id: pr88934
    resource: https://github.com/godotengine/godot/pull/88934
    title: godotengine/godot PR 88934, mesh shader support for RenderingDevice
  - id: mvk2642
    resource: https://github.com/KhronosGroup/MoltenVK/pull/2642
    title: MoltenVK PR 2642, mesh shader support (WIP, 2025)
  - id: mesa262
    resource: https://phoronix.com/news/Mesa-26.2-Released
    title: Phoronix, Mesa 26.2 released with NVK mesh shader support
---

# Summary

Read 2026-11-26 (online research, no code written, nothing measured here).

## Hard API limits

D3D12 (spec v0.86):[^d3d12spec]

| Limit | Value |
| --- | --- |
| Threads per AS or MS group | 128 |
| Output vertices / primitives per MS group | 256 / 256 |
| MS output size | `vert_bytes*ALIGN32(nv) + prim_bytes*ALIGN32(np) <= 32 KB` |
| Attribute rows (vertex + primitive) | 32 float4 rows |
| AS to MS payload | 16 KB |
| Groupshared | MS 28 KB, AS 32 KB |
| DispatchMesh | each dimension < 64k, product <= 2^22 |
| Indirect | `D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH` via ExecuteIndirect |
| Caps query | `D3D12_FEATURE_D3D12_OPTIONS7` `MeshShaderTier` |

Ordering: within a group, primitives rasterize in write order; groups in
(x,y,z) order; mesh groups launched by the same AS group may retire in any
order.

Vulkan `VK_EXT_mesh_shader`: limits are per device in
`VkPhysicalDeviceMeshShaderPropertiesEXT`. AMD, Intel and NVIDIA expose
256 vertices / 256 primitives, the de-facto common floor.[^zeux]
Indirect: `vkCmdDrawMeshTasksIndirectEXT` and `...IndirectCountEXT`.[^vkext]
Metal 3 has the same model (object + mesh stage).[^metal]

Stage names: task (Vulkan) = amplification (D3D12) = object (Metal).

## Meshlet sizing

* NVIDIA: 64 vertices, 84 or 126 triangles (output granularity:
  84*3 = 252 B, 126*3 = 378 B of indices).[^zeux][^nvapi]
* AMD: 128 vertices, 256 triangles, `numthreads(128,1,1)`, thread i writes
  vertex i and primitive i; 64/126 is about as fast but duplicates more
  vertices.[^gpuopen]
* Intel: no published numbers found.
* Vendors differ in shape too: NVIDIA prefers small groups with several
  outputs per thread, AMD large groups with one output per thread.[^vkext]
* Below 32 threads per group (8 or 16 triangle meshlets) wastes lanes.[^zeux]

Transform reuse (best grid meshlets): V64/T98 ACMR 0.65, V128/T200 0.60,
V256/T450 0.57.[^zeux] Bunny: meshlets transform 1.24x the unique vertices,
a vertex-cache-optimized index buffer 1.40x.[^gpuopen]

## Culling efficiency vs meshlet size (Zeux test scene)

| Culling | 64 tri | 256 tri |
| --- | --- | --- |
| Cone (backface), with LOD | 14% | 3% |
| Cone, without LOD | 32% | 21% |
| Occlusion | ~80% | ~66% |

Larger meshlets lose more culling than they gain in transform reuse.[^zeux]

## Amplification / task shader advice

* Process 32-64 meshlets per AS group; compact with wave prefix ops.[^gpuopen]
* One AS group should launch at least 32 MS groups.[^gpuopen]
* Keep the payload small: it goes through memory.[^gpuopen]
* Do per-object culling with draw-indirect, not one task invocation per
  object.[^nvapi]

## Mesh shaders vs compute culling + indirect draws

* NVIDIA: gains are not guaranteed over the classic pipeline.[^nvapi]
* Microsoft argues compute + ExecuteIndirect pays a UAV barrier between
  cull and draw that a mesh pipeline avoids.[^xbox]
* Tellusim (2021, dense unculled scene, several vendors): best mesh shader
  and MDI results were both slower than compute rasterization.[^tellusim]
* No recent cross-vendor benchmark with realistic culling was found.
* Nanite rasterizes small triangles in compute and uses the hardware path
  (mesh / primitive shaders) only for large ones.

## Platform coverage

* NVIDIA Turing+, AMD RDNA2+, Intel Arc (Linux ANV on by default). NVK has
  it since Mesa 26.2.[^mesa262]
* Mobile Vulkan (Adreno, Mali): no evidence found of `VK_EXT_mesh_shader`.
* MoltenVK: PR 2642 (2025) was work in progress, CTS failed.[^mvk2642]
  KosmicKrisp: no evidence found.

## Godot RenderingDevice

PR 88934 (open, not merged as of 2026-11):[^pr88934]

* Adds `SHADER_STAGE_MESH_TASK`, `SHADER_STAGE_MESH`, `#[mesh_task]` /
  `#[mesh]` sections, `draw_list_dispatch_mesh()`, `..._indirect()`,
  `..._indirect_count()`; a render pipeline takes a mesh stage instead of
  a vertex stage.
* Vulkan works. D3D12 untested: Godot's SPIR-V to DXIL (Mesa NIR) path has
  no mesh or amplification stages. Metal stubs only.
* No engine fallback; custom shaders only. No maintainer merge decision
  since 2024.

# What it means here

* [D002](../../knowledge/TECHNICAL_DECISIONS.md) (compute + indexed
  indirect before mesh shaders) still holds. Our default D3D12 path could
  not use mesh shaders through RD without new DXIL work, and Metal and
  mobile lack them.
* If tried later, it is an optional Vulkan fast path for CLOD clusters:
  meshlets 64v/126t (NVIDIA) or 128v/256t (AMD), task shader doing cone +
  HZB culling of 32-64 clusters per group, keeping the existing compute
  path as fallback. Take PR 88934 as the RD base. Compare against our
  compute culling on the same scene before keeping it.
* Not measured on this fork. No numbers above are Godot numbers.
