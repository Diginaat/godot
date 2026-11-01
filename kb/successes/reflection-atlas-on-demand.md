---
type: Success
title: Reflection atlas layers on demand
description: Growing the reflection probe atlas by doubling, with layer copies, cut its memory from 512 MB to 47 MB for one probe at the default settings.
tags: [success, reflections, memory]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
---

# What worked

Godot allocated all `reflection_count` (64) layers of the reflection atlas
when the first probe rendered: 512 MB at the default 256 size (694 MB in
realtime mode). The atlas now starts with 4 layers and doubles when no
slot is free, copying the captured layers with `texture_copy` so nothing
is captured again. RTX 3060, `reflect` test view: total VRAM 877 MB to 411
MB with one probe; 504 MB with 13 probes (16 layers). The main probe's
image is unchanged across a growth.

# Why it worked

The cost was a fixed allocation, not work: measuring VRAM per feature
toggle (not just GPU time) found it. Copying instead of recapturing keeps
growth invisible.

# Reuse

Any fixed-size atlas or array sized by a project limit: allocate for what
is used, grow geometrically, copy.
