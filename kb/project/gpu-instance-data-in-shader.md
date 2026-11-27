---
type: Finding
title: The GPU path kept a 176-byte instance copy per member that the scene shader can build itself
description: Every frame the GPU instance cull wrote SceneState::InstanceData (176 B) for every live member, 55 MB at 200k nodes, copied from the instance table record, the member and its bucket. The scene shader now builds that data from those three buffers and a 16-byte view flags buffer when it draws. city_extreme 287 to 232 MB with 4% less GPU time, massive_100k 0.948 to 0.903 ms, images and motion vectors equal to the CPU path.
tags: [vram, gpu-instances, shaders, bandwidth, large-worlds]
status: stable
sources:
  - id: level
    title: misc/stock_compare_project/level_bench.gd --frames=300 (town, forest, city_extreme), builds before and after alternated, two rounds, Vulkan, RTX 5070, 2026-11-27, branch dev-renderer-next
  - id: instance
    title: misc/large_world_test_project/instance_bench.gd --case=small_1k,massive_100k,churn_100k --table --gpu-instances --occlusion, same session
  - id: tests
    title: devtools/test/gpu_instances.ps1 on Vulkan and D3D12, and on Vulkan with every pipeline forced to the ubershader
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

After OD9, city_extreme (200k nodes) used 287 MB of VRAM against 188 MB
in stock 4.7.2, and 139 MB of it was buffers (stock: 30). The largest
buffer was `GPU Instances Instance Data`: one `SceneState::InstanceData`
(176 B) per member (an instance surface), 55 MB. The camera view's cull
pass wrote it for every live member each frame, visible or not, because
the shadow passes draw from it too.

Every field of that copy comes from three buffers the GPU path already
has:

- the instance table record (transform, previous transform, flags,
  instance uniforms offset, layer mask, lightmap UV scale),
- the member (table slot, bucket, material table slot),
- the bucket (compressed mesh AABB, material feedback index, UV scale),

plus three view flags (GI buffers, SDFGI, motion pass) and constants
(fade 255, no GI offset).

# Change

- `scene_forward_clustered_inc.glsl`: binding 47 is the instance table,
  49 the members, 50 the buckets and 51 a 16-byte view flags buffer.
  `gpu_instance_data()` builds the `InstanceData` the cull pass wrote.
  `scene_instance_load()` now runs in every pipeline: in the GPU path it
  builds the instance once per stage into `scene_instance`; specialized
  CPU-path pipelines still read `instances.data[]` directly (the
  specialization constant folds the branch away).
- `gpu_instance_cull.glsl`: no instance data output (binding 4 removed).
- `gpu_instances.cpp`: no `output_buffer`. The camera view writes its
  flags into the scenario's `view_buffer` (`buffer_update`, 16 bytes).
  Shadow passes read none of those flags, as before.
- `RenderForwardClustered::GPUInstanceBindings` carries members, buckets
  and view to `_setup_render_pass_uniform_set()` and the shadow passes.

# Results

Level bench, `--frames=300`, two rounds with the builds alternated (both
rounds gave the same VRAM):

| Preset | Stock 4.7.2 | Before | After | GPU p50 before / after (ms) |
| --- | --- | --- | --- | --- |
| town | 167 | 186 | 175 | 0.716 / 0.706-0.758 |
| forest | 158 | 165 | 149 | 1.672 / 1.622 |
| city_extreme | 188 | 287 | 232 | 3.455 / 3.313 |

Instance bench, static view:

| Case | Video before / after (MB) | GPU p50 before / after (ms) |
| --- | --- | --- |
| small_1k | 70.2 / 69.9 | 0.167-0.193 / 0.166-0.194 |
| massive_100k | 131.8 / 107.0 | 0.948 / 0.903 |
| churn_100k | 160.4 / 135.7 | 0.872 / 0.823 |

CPU time is unchanged. The cold shader stall (`shader_bench.ps1 -Cold
-Mv`, 120 materials) is unchanged: 44.9 and 44.6 s before, 44.4 and
45.2 s after. `gpu_instances.ps1` passes on Vulkan and D3D12: 15 images
and the TAA and FSR 2 motion vectors equal to the CPU path. It passes
too with every pipeline forced to the ubershader.

# Why it is faster

Each member's 176-byte write per frame (17 MB at 100k members) is gone.
The draws read the record, member and bucket instead, and only for the
instances they draw.

# Still open

city_extreme is still 44 MB above stock: the instance table (35 MB), the
member and ID lists, and the HZB. The table could drop the previous
transform for instances that never move: done in OD12
([prev-transform-table](/project/prev-transform-table.md), 232 to 222 MB).
