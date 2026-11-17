---
type: Finding
title: Forward+ shader variants and pipelines compiled only where used
description: Feature groups, per-shader skipped variants and pruned transparent pipeline precompiles cut Forward+ variants per material from 28 to about 4, the cold stall of adding 120 materials from 146 to 95 s (Vulkan) and 239 to under 97 s (D3D12), warm from 2.4 to 0.65 s, and memory from 353 to 84 MB, with identical images.
tags: [rendering, shaders, pipelines, forward-plus, vulkan, d3d12, performance, memory]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-17T00:00:00Z }
sources:
  - id: memory
    resource: ../../docs/shader-permutations/memory.md
    title: Working memory with audit, results tables, decisions and proposal
  - id: bench
    resource: ../../devtools/test/shader_bench.ps1
    title: Shader and pipeline compilation benchmark (cold clears every cache layer)
---

# What was measured

`shader_bench`: 120 distinct StandardMaterial3D shaders added in one frame
under shadowed sun, omni and spot lights; RTX 3060, 12 threads. Cold runs
clear Godot's shader and PSO caches, the project's baked cache and the GPU
driver's disk caches. Counters print with `--verbose` (`SHADER_STATS`,
`SHADER_VARIANTS` built/used per variant, `PIPELINE_STATS`,
`FORWARD_PIPELINE_STATS`).

Baseline (Vulkan, motion vectors): 28 scene variants built per material, 6
used; adding the materials stalled one frame 146 s cold (driver pipeline
builds 1577 s CPU) and 2.4 s warm (creating 3556 variants from the cache).

# What changed

- Freed compiled shader bytes after each group and the loaded Vulkan PSO
  cache after `vkCreatePipelineCache` (warm memory 353 to 101 MB).
- Variant groups by required feature (motion vectors no longer pull in
  lightmap, separate specular, GI and bake variants), with an on-demand
  safety net.
- No precompiled transparent pipelines with motion vectors or separate
  specular (no transparent pass uses them).
- Pipeline maps keyed by the whole key, not a 32-bit hash.
- Per-shader skipped variants: depth that a shader never draws (shared
  shadow material, alpha-only), motion-vector and separate-specular color
  for alpha-only shaders.
- Fix: SDFGI pass uniform set lacked bindings 47/48 (SDFGI draws failed).

# Results

| | Base | Now |
| --- | --- | --- |
| Scene variants (Vulkan, MV) | 3556 | 472 |
| Cold stall, Vulkan | 145.8 s | 94.7 s |
| Cold stall, D3D12 | 238.7 s | 97.1 s (before the skip rules) |
| Warm stall, Vulkan | 2.40 s | 0.65 s |
| Shader cache, Vulkan | 104 MB | 17.7 MB |
| Memory end, Vulkan warm | 353 MB | 84 MB |
| GPU frame p50 | 1.65 ms | 1.66 ms |

Images pixel-identical with and without motion vectors, SSR, VoxelGI, SSS +
dual paraboloid + SDFGI (SDFGI within its own A/A noise of 1-2/255 after
the binding fix made it work).

# What is left

The cold stall is now 90% ubershader pipeline builds (3.24 s each against
0.30 s for a specialized pipeline). A specialized-first schedule could cut
the first-draw stall to about a tenth at the cost of minutes of background
compiling; written up as a proposal in the working memory.
