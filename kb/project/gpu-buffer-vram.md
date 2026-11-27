---
type: Finding
title: VRAM of the GPU-driven systems cut by growth policy and a half-resolution HZB
description: The fork used 35 to 285 MB more VRAM than stock 4.7.2 in the level bench. Power-of-two buffer growth doubled the large GPU instance buffers past a boundary, and the HZB copied the depth buffer at full resolution padded to powers of two. An eighth of headroom above 65,536 elements and an HZB starting at half resolution cut city_extreme from 475 to 415 MB with slightly lower GPU time.
tags: [vram, gpu-instances, hzb, occlusion, memory]
status: stable
sources:
  - id: bench
    title: misc/stock_compare_project/level_bench.gd (town, forest, city_extreme), 2026-11-27, branch dev-renderer-next, RTX 5070, 1280x720, Vulkan
  - id: nanite
    title: Karis, Stubbe, Wihlidal, A Deep Dive into Nanite Virtualized Geometry, SIGGRAPH 2021 (HZB from the depth buffer at half resolution)
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

The stock comparison ([stock-compare-2026-11](/project/stock-compare-2026-11.md))
showed the fork using more VRAM than stock in every level. Two findings:

- **Small scenes.** With every GPU-driven feature off, a 1,000-node scene
  used exactly what stock uses (68.8 MB). The default settings added
  11.4 MB, almost all of it the HZB: an R32F pyramid of the depth buffer
  padded to powers of two (2048 x 1024 with 12 mips at 720p).
- **Large levels.** In city_extreme (200k nodes) the biggest buffers were
  the per-member instance data (176 B each) and the instance table
  (160 B per slot). Power-of-two growth gave 262,145 members 524,288
  slots: 88 MB of instance data for 46 MB in use.

The 144 MB extra that the instance bench reported at 1k nodes is not a
cost of small scenes. It is capacity left from its larger cases, which run
in the same process.

# Changes

- `gpu_buffer_capacity()` (`gpu_buffer_capacity.h`): powers of two up to
  65,536 elements; above that, an eighth of headroom rounded up to a
  multiple of 16,384. Used by the instance table, the GPU instance path,
  mesh occlusion, MultiMesh culling and cluster LOD. The table first had a
  quarter of headroom, but 200k x 1.25 rounded to the same 262,144 slots.
- The HZB's mip 0 is the farthest depth of each 2 x 2 pixels
  (`MODE_FIRST` in `mesh_hiz_reduce.glsl`). `hzb_box_occluded()` keeps
  full-resolution coordinates, starts at level 1 and reads mip
  `level - 1`. The test stays conservative, because a coarser texel holds
  a farther or equal depth. `hzb.gd` checks the new mip 0 against a CPU
  reduction.
- The GPU instance buffers carry resource names (for example
  "GPU Instances Instance Data"), so RenderDoc and memory tools show them.

# Results

VRAM (`RENDER_VIDEO_MEM_USED`, MB) and GPU p50 (ms), 300 frames, two runs:

| Preset | Stock | Before | Growth only | Growth and HZB | GPU, growth only / both |
| --- | --- | --- | --- | --- | --- |
| town | 167 | 201 | 200 | 192 | 0.706 / 0.687 |
| forest | 158 | 237 | 229 | 221 | 1.430 / 1.411 |
| city_extreme | 188 | 475 | 423 | 415 | 3.335 / 3.309 |

The 1,000-node scene went from 80.2 to 72.2 MB (stock 68.8).
`mesh_occlusion_bench` counted the same occluded meshes and instances.
Its street view stayed at about 0.53 ms of GPU time with occlusion on.

# Open

- The per-member instance data (55 MB at 200k nodes): gone, the scene
  shader builds it ([gpu-instance-data-in-shader](/project/gpu-instance-data-in-shader.md), OD10).
- Buffers never shrink after a level is unloaded: done
  ([vram-shrink-after-unload](/project/vram-shrink-after-unload.md), OD9).
