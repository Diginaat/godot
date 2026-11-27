---
type: Finding
title: VRAM stayed at the largest scene's size after it was unloaded
description: After a 100,000-node scene was freed, a 1,000-node scene in the same process used 300 MB of VRAM against 72 MB fresh. Four things only grew. The GPU instance buffers and their slot range kept their peak size, and so did the CPU render list instance buffers. The RenderingDevice upload staging ring kept the blocks a burst of uploads added. The transfer workers' staging buffers stayed at the size of the largest transfer ever made. All four now shrink. 300 to 83 MB after the unload; the level bench 415 to 287 MB (city_extreme) and 221 to 165 MB (forest).
tags: [vram, gpu-instances, staging, rendering-device, memory, small-scenes]
status: stable
sources:
  - id: bench
    title: misc/large_world_test_project/instance_bench.gd --case=small_1k,massive_100k --table --gpu-instances --occlusion, Vulkan and D3D12, RTX 5070, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

`instance_bench.gd` runs its cases in one process: small_1k, then
massive_100k twice, then small_1k again. The second small_1k run used
far more VRAM than the first (`Performance.RENDER_VIDEO_MEM_USED`, MB):

| Run | Video | Buffers | Textures |
| --- | --- | --- | --- |
| small_1k, fresh | 72.2 | 15.0 | 52.2 |
| small_1k after massive_100k, before | 300 | 84-104 | 52.2 |

Textures were the same, so the rest was buffers and other allocations.
Every buffer created and freed was logged temporarily (size, name, RID)
and the live set compared between the two small_1k runs.

# Why

Four allocators only grew:

1. **GPU instance slots and buffers.** Freed slots went on a LIFO free
   list, so the slot range never shrank, and the instance table, scenario
   members, two-pass history and the visible list grew but never shrank.
2. **The CPU render list instance buffers**
   (`SceneState::instance_buffer[]`, 176 B per element). Grown to a power
   of two for the largest frame, and kept. One list stayed at 131,072
   elements (22 MB) while small_1k filled it with nothing: the empty fill
   returned before any size check.
3. **The RenderingDevice upload staging ring.** Building massive_100k
   uploads more than one frame's blocks, so the ring added 256 KB blocks
   up to 139 (35 MB). Upstream never frees them. The ring hands every
   block out in turn, so none of them ever looks idle.
4. **The RenderingDevice transfer workers.** A buffer or texture created
   with data is copied through a worker's own staging buffer. The buffer
   is the next power of two of the largest transfer the worker ever made,
   and stays. This was neither buffer nor texture memory in the monitors,
   only in the video total: 33 MB after massive_100k, and more after
   loading a level with large meshes and textures.

# Fix

- **Slots:** the lowest free slot is taken (a bitmap), and the slot
  range ends at the highest slot in use. The instance table is truncated
  to it.
- **GPU instance buffers:** `gpu_buffer_fits()` in
  `gpu_buffer_capacity.h`: a buffer over 16,384 elements is recreated
  when less than a quarter of it is needed. It is recreated at
  `gpu_buffer_capacity()`, which leaves at least an eighth of headroom,
  so content near a boundary does not resize back and forth. Only buffers
  sized by the scene's content use it, not the shadow batch buffers that
  one frame sizes.
- **Render list instance buffers:** each list keeps the largest element
  count of the frame, with shadow passes appended to it. After 120 frames
  in a row below a quarter of the buffer (and over 16,384 elements), the
  frame's first fill recreates it at the power of two of the last peak.
  Empty fills count too.
- **Staging ring:** `RenderingDevice::_trim_staging_blocks()` counts the
  blocks each frame claims. It keeps the largest count for each of five
  windows of 60 frames. Every 60 frames, if the ring holds more than
  twice the busiest frame's need times the frames in flight, blocks that
  no frame in flight can read are freed down to that need. Download
  blocks are left alone: pending readbacks keep their IDs.
- **Transfer workers:** `RenderingDevice::_trim_transfer_workers()`
  checks every 60 frames. A worker with no new operation for five checks
  (300 frames) and a staging buffer over 64 KB frees it. It waits for the
  worker's fence first, which was signaled long ago. A worker in use by
  another thread is skipped (`try_lock()`). The next transfer creates a
  buffer of the size it needs.

# Results

Same bench, Vulkan (video MB):

| Run | Before | Buffers and staging ring | All four | Buffers after |
| --- | --- | --- | --- | --- |
| small_1k, fresh | 72.2 | 72.2 | 70.2 | 15.0 |
| massive_100k, second run | 244 | 168 | 136 | 78.1 |
| small_1k after massive_100k | 300 | 115 | 83.2 | 25.4 |

D3D12 gives the same within 1 MB (83.8 after). GPU time is unchanged
(small_1k 0.166 ms, massive_100k 0.78-0.95 ms, as before). The 100k
scene itself got smaller because its CPU list buffer was mostly unused.

Level bench (`misc/stock_compare_project/level_bench.gd --frames=300`),
VRAM MB and GPU p50, against the build before, run in the same session:

| Preset | Stock 4.7.2 | Before | After | GPU p50 before / after (ms) |
| --- | --- | --- | --- | --- |
| town | 167 | 192 | 186 | - / 0.712 |
| forest | 158 | 221 | 165 | 1.678 / 1.679 |
| city_extreme | 188 | 415 | 287 | 3.486 / 3.490 |

The level gains are the transfer workers: the staging buffers made for
loading the level are freed 300 frames after loading ends. With
`--frames=100` town still reads 192 MB.

# Still open

The second small_1k run still uses 13 MB more than a fresh one: about
10 MB of buffers (six unnamed storage buffers of 0.4 to 2.8 MB, one of
them a 16,384-element render list buffer, below the shrink size).

Letting render list buffers shrink down to 4,096 elements took the
second small_1k run from 82.9 to 78.7 MB and town from 174 to 169 MB,
but town's render CPU went from 0.603 to about 0.63 ms (10 alternated
rounds). Each list shrank once, at frame 123, and never grew back, so
the cost is not reallocation; the cause was not found, and the change
was dropped. Two copies of the same exe later gave town 0.603-0.623 and
0.610-0.638 ms over four rounds, so town's render CPU spreads by about
0.03 ms between runs; the change was slower in 8 of 10 rounds (medians
0.602 and 0.631), so it may be worth measuring again with more rounds.

# Settings

None. Every fallback is kept: a buffer grows as before, and a staging
block or worker buffer is only freed when no GPU work can still read it.
The cost is a staging buffer created again when a large transfer comes
after 300 idle frames.
