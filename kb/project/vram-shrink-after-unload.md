---
type: Finding
title: VRAM stayed at the largest scene's size after it was unloaded
description: After a 100,000-node scene was freed, a 1,000-node scene in the same process used 300 MB of VRAM against 72 MB fresh. Three things only grew. The GPU instance buffers and their slot range kept their peak size. The CPU render list instance buffers did too. The RenderingDevice upload staging ring kept the blocks a burst of uploads added. All three now shrink: 300 to 115 MB, and the 100k scene itself 244 to 168 MB.
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

Three allocators only grew:

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

# Results

Same bench, Vulkan:

| Run | Video before | Video after | Buffers after |
| --- | --- | --- | --- |
| small_1k, fresh | 72.2 | 72.2 | 15.0 |
| massive_100k, second run | 244 | 168 | 78.1 |
| small_1k after massive_100k | 300 | 115 | 25.4 |

D3D12 gives the same within 1 MB (115.8 after). GPU time is unchanged
(small_1k 0.166 ms, massive_100k 0.78-0.95 ms, as before). The 100k
scene itself got smaller because its CPU list buffer was mostly unused.

# Still open

The second small_1k run still uses 43 MB more than a fresh one: about
10 MB of buffers (six unnamed storage buffers of 0.4 to 2.8 MB) and
about 33 MB that is neither buffers nor textures.

# Settings

None. Every fallback is kept: a buffer grows as before, and a staging
block is only freed when no frame in flight can read it.
