---
type: Finding
title: Scrolled probes traced at another probe's position (GLSL negative modulo)
description: With infinite_world, ddgi_probe_logical() used % on negative values; NVIDIA computes it as unsigned, so after a scroll some probes were traced, relocated and classified at another probe's position.
tags: [ddgi, scrolling, infinite-world, glsl, bugfix]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# Symptom

An `infinite_world` `DDGIVolume` in a C# test level: after walking about
15 m, 2 m brown squares appeared on an empty floor and stayed while the
camera stood still. The Indirect Light debug view showed them black; the
Probe States view showed a row of red (inside) probes on the floor.
Hiding two flights of stairs 11 m away removed them.

# Cause

`ddgi_probe_logical()` (`ddgi_inc.glsl`) turned a probe's storage index
into grid coordinates with `((a % n) + n) % n`, where `a = storage -
scroll` is negative for every probe stored before the scroll offset. GLSL
leaves `%` undefined for negative operands. The NVIDIA driver computes it
as unsigned: `-15 % 29` gives 1 instead of 14. The trace, relocate and
debug passes use this function, so such probes were traced from another
probe's position (here, inside the stairs) and classified as inside. The
sampling side (`ddgi_probe_index()`) was right, so the wrong state showed
at the right place: black squares that follow the scroll offset.

Found by reading the probe buffer back on the CPU: each wrong probe had
the exact relocation offset of a probe inside the stairs, 16 storage
slabs away.

# Fix

Both terms are in `0..grid-1`, so the difference is corrected with a
conditional add of the grid size, as `ddgi_probe_index()` already does
the other way. No `%` on signed values in DDGI shaders.

Verified on Linux, RTX 3060, Vulkan, the sandbox level at the reported
spot (walked and jumped there, 960x540): no black pixels in the Indirect
Light view, no inside probes on the floor; smoke tests pass.
