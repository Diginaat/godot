---
type: Finding
title: Every instance table record carried a previous transform that only moving instances need
description: GPUInstanceTable::Record held a 48-byte previous transform (motion vectors) for every instance, though for any instance that did not move last frame it equals the transform. It now lives in a separate table with entries only for instances whose previous transform differs, and the record (160 to 112 bytes) points to its entry. city_extreme 232 to 222 MB, massive_100k 107 to 102 MB, churn_100k 131.5 to 120.3 MB, GPU time unchanged. A first version leaked span: entries of instances that stopped moving left holes, so a fixed set of 2000 movers uploaded 20,000 entries per frame (+5% GPU); the table is compacted when fewer than half its used entries are live.
tags: [vram, gpu-instances, motion-vectors, large-worlds]
status: stable
sources:
  - id: level
    title: misc/stock_compare_project/level_bench.gd --frames=300 (town, forest, city_extreme), builds before and after alternated, two rounds, Vulkan, RTX 5070, 2026-11-27, branch dev-renderer-next
  - id: instance
    title: misc/large_world_test_project/instance_bench.gd --case=small_1k,dynamic_20k,massive_100k,churn_100k --table --gpu-instances --occlusion, same session; dynamic_20k also alone and after small_1k, 3-4 rounds
  - id: tests
    title: devtools/test/gpu_instances.ps1 on Vulkan and D3D12 and on Vulkan with every pipeline forced to the ubershader; instance_table.ps1; large_world.ps1; smoke.ps1
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

After OD10, city_extreme used 232 MB against 188 MB in stock 4.7.2. The
largest fork-only buffer was the GPU instance table: one 160-byte record
per instance slot, 35 MB at 200k nodes. 48 bytes of each record were the
previous transform, read only by the scene shader for motion vectors.

`GeometryInstanceForwardClustered::prev_transform` equals `transform`
except on the frames right after a move (`set_transform()` sets it, and
`age_out_motion()` resets it one frame later and marks the record dirty).
A static level has almost no such instances.

# Change

- `GPUInstanceTable::Record` drops `prev_transform`: 112 bytes
  (`gpu_instance_cull.glsl`, `gpu_instance_scatter.glsl` and
  `scene_forward_clustered_inc.glsl` follow).
- `GPUInstancePrevTable`: 48-byte entries, allocated lowest first with a
  `GPUInstanceSlots`, a CPU mirror, and one `buffer_update` per frame for
  the span of entries written. Its buffer grows and shrinks with
  `gpu_buffer_capacity()` / `gpu_buffer_fits()` like the other content
  buffers.
- When a record is rebuilt, `_gpu_instance_table_build_record()` reports
  whether the previous transform differs from the transform. If so, the
  slot gets (or keeps) an entry, and `gpu_flags` holds the entry plus one
  from bit 8 (`GPU_FLAG_PREV_SHIFT`). Otherwise the entry is freed.
  Freed slots, and slots past the highest in use, free theirs too.
- Scene shader binding 52 (`gpu_instance_prev`); `gpu_instance_data()`
  reads the entry when `gpu_flags >> 8` is not 0 and uses the transform
  otherwise.
- Compaction: when the highest entry in use is more than twice the live
  entries plus 256, every entry is freed and its record marked dirty; the
  rebuild that frame packs them from 0.

# The span leak (first version)

Every instance has a differing previous transform on the frame it is
created (from the default transform), so all 20,000 instances of
`dynamic_20k` got entries. The 18,000 static ones freed theirs two frames
later. But the 2,000 that move every frame kept entries spread up to
19,991, so the buffer stayed at 32,768 entries and each frame uploaded a
960 KB span. Measured after `small_1k` in the same process: GPU 0.701 to
0.736 ms, render CPU +0.03 ms, video +1.4 MB. Run alone, the case
happened to show no difference, which hid it at first. With compaction:
2,000 entries in a 2,048-entry buffer, GPU 0.701 to 0.700 ms, video
-1.5 MB.

# Results

Level bench (two rounds, identical VRAM in both) and instance bench
(static view, two repeats in two rounds):

| Case | VRAM before / after (MB) | GPU p50 before / after (ms) |
| --- | --- | --- |
| town | 175 / 175 | 0.715-0.719 / 0.719-0.724 |
| forest | 149 / 146 | 1.636-1.652 / 1.644-1.646 |
| city_extreme | 232 / 222 | 3.375-3.379 / 3.369-3.426 |
| small_1k | 69.9 / 69.9 | 0.167 / 0.167 |
| dynamic_20k | 86.7 / 85.2 | 0.696-0.701 / 0.698-0.701 |
| massive_100k | 107.0 / 101.8 | 0.890-0.909 / 0.888-0.910 |
| churn_100k | 131.5 / 120.3 | 0.830-0.980 / 0.830-0.977 |

Stock 4.7.2: town 167, forest 158, city_extreme 188 MB.

# Tests

`gpu_instances.ps1` passes on Vulkan and D3D12 (15 of 15 images, TAA and
FSR 2 motion vectors equal to the CPU path), and on Vulkan with every
pipeline forced to the ubershader. With the shader made to ignore the
entries, the motion vector comparison fails (about 900-1,000 values per
frame differ), so the test covers the new path. `instance_table.ps1`
(including its self-test), `large_world.ps1` and the smoke tests pass.

# Then: no lightmap UV scale (OD13)

The record's 16-byte lightmap UV scale had no reader either: the scene
shader reads it only with `INSTANCE_FLAGS_USE_LIGHTMAP`, and the GPU path
(the only reader of records) takes no lightmapped instances
(`_gpu_instance_eligible()`). The record is now 96 bytes, three 32-byte
sectors; `gpu_instance_data()` writes a constant. The element check no
longer compares the field.

| Case | VRAM OD12 / OD13 (MB) | GPU p50 OD12 / OD13 (ms) |
| --- | --- | --- |
| town | 175 / 174 | 0.721-0.723 / 0.724-0.784 |
| forest | 146 / 145 | 1.637-1.655 / 1.640-1.656 |
| city_extreme | 222 / 219 | 3.355-3.363 / 3.366-3.378 |
| dynamic_20k | 85.2 / 84.7 | 0.696-0.701 / 0.694-0.698 |
| massive_100k | 101.8 / 100.1 | 0.886-0.910 / 0.884-0.903 |
| churn_100k | 120.3 / 116.6 | 0.828-0.974 / 0.826-0.971 |

`gpu_instances.ps1` (Vulkan, D3D12), `instance_table.ps1`,
`large_world.ps1` and the smoke tests pass.
