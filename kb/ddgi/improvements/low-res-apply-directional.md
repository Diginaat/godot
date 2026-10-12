---
type: Proposal
title: Low-resolution apply with directional output
description: Run the DDGI apply pass at 1/4 resolution on a downsampled G-buffer, output L1 SH per pixel, and upsample depth-normal aware, to cut its cost while keeping normal-map detail.
tags: [ddgi, performance, proposal, upsampling]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: downscale
    resource: /research/gi-downscaling.md
    title: Global illumination downscaling
  - id: sh
    resource: /research/stupid-sh-tricks.md
    title: Stupid SH Tricks
---

# Result (2026-10-12, branch `dev-ddgi-quality`)

Built as `rendering/global_illumination/ddgi/apply_resolution` (Full, 1/2,
1/4, 1/8; default 1/4). Instead of L1 SH, each block stores the
irradiance at its normal plus the gradient over the normal, from four
directions tilted around it (exact at the block normal, linear around
it). Block surfaces use the min/max depth checkerboard from
[SDFDDGI](/research/sdfddgi.md). 1080p, RTX 3060: apply 0.717 -> 0.372 ms
at 1/4, 0.309 at 1/8;
GI buffer within 0.3 levels of full resolution in the interior. The
full resolution upsample (about 0.2 ms) is the floor. Details and the
cost breakdown: DDGI.md findings log, step 23;
[the finding](/ddgi/findings/full-res-pass-floor.md).

# Before

The apply pass samples the probes once per pixel at internal resolution:
0.66 of 1.33 ms DDGI time at 1080p on an RTX 3060, the largest single
pass. `use_half_resolution` runs it at half resolution (about a quarter of
the cost) with softer light at object edges.

# Proposal

From the GI downscaling article:[^downscale]

1. Average depth and normals 2x2 (or 4x4) down to the apply resolution.
   Averaged inputs make the low-resolution result stable without temporal
   filtering.
2. At each low-resolution pixel, sample the probes for the whole sphere
   and store L1 SH (4 RGB coefficients) instead of one irradiance value.
   Keep the cosine-convolved SH non-negative (clamp or window, one window
   for all channels).[^sh]
3. Upsample with a depth-normal aware filter and evaluate the SH with the
   full-resolution normal, so normal maps keep their indirect shading.

Godot already has a half-resolution GI path (`using_half_size_gi`) that
the scene shader upsamples; this would add a second, lower setting with
directional output.

# Measure

- Apply cost at 1080p and 4K, full / half / quarter.
- Edge halos and normal-map detail against full resolution (screenshots,
  difference image), flicker (6 runs), motion (outdoor `--move=1`).
- Whether 6 floats per pixel more of bandwidth eat the gain.

[^downscale]: Global illumination downscaling
[^sh]: Stupid SH Tricks
