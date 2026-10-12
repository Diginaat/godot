---
type: Mistake
title: Mixed up an encoding convention
description: A normal encoded with vec3_to_oct (0..1) was decoded with oct_to_vec3 (expects -1..1); the low resolution apply came out 12% too dark with banding.
tags: [mistake, shaders, encoding]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

The low resolution apply stores each block's normal in octahedral form
with `vec3_to_oct()` from `oct_inc.glsl` and decodes it in the upsample
with `oct_to_vec3()`. The first returns 0..1, the second expects -1..1.
The decoded normals were wrong, so the normal match and the gradient term
were wrong. Even at 2x2 blocks, the GI buffer was 12% darker than at full
resolution, with banding. The comparison against full resolution caught
it, after a full build and test run.

# Rule

- Before pairing two helpers (encode/decode, pack/unpack), read both and
  check the value range each one uses. Names don't say it.
- Test a new path against the old one at its mildest setting (here 2x2
  blocks) first: a large difference there is a bug, not a quality trade.
