---
type: Finding
title: A full resolution pass has a floor of about 0.2 ms
description: At 1080p on an RTX 3060, reading depth and normals and writing the two GI buffers per pixel costs about 0.2 ms; the low resolution apply can't go below it, and per-pixel reads inside a low resolution pass add up the same way.
tags: [ddgi, performance, gpu]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md findings log, step 23
---

# What was measured

The low resolution apply at 1/4 cost 0.43 ms at first, against 0.72 at
full resolution. Temporary builds split it up (1080p room, RTX 3060):

| Variant | Apply ms |
| --- | --- |
| Full low resolution apply + upsample | 0.43 |
| Upsample only writes (no reads, no math) | 0.38 |
| Low resolution pass returns at once | 0.23 |
| One lookup direction instead of five | 0.41 |
| Shared memory for the upsample's block reads | 0.43 |
| Averaging 2x2 pixels per block instead of all 16 (shipped) | 0.40 |

- The upsample, at full resolution, costs about 0.2 ms: one depth and one
  normal read, the view position, and two RGBA16F writes per pixel. Its
  block reads and math are small.
- The low resolution pass first read all 16 pixels of each 4x4 block (64
  for 8x8): that's a full resolution read pass again, about 0.15 ms, and it
  made 1/8 slower than 1/4.
- The probe sampling itself is the cheap part at low resolution: five
  directions cost 0.02 ms over one, because the probe weights are shared.

# How to apply

- Before optimizing a pass, measure variants that remove one part each
  (writes only, early return, fewer directions). Shared memory was a guess
  and changed nothing.
- In a low resolution pass, count the full resolution reads: they cost
  like a full resolution pass.
- The remaining floor could go only by not writing the reflection buffer
  every frame (it is constant for DDGI) or by a lower GI buffer resolution.
