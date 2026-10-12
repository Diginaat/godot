---
type: Proposal
title: Near-field rays for the DDGI apply pass
description: One short ray per low resolution block and frame, shaded like a probe ray, accumulated and filtered, in place of the probes' light, so bounce light from nearby objects sits where it belongs and follows them.
tags: [ddgi, visuals, moving-objects, denoising]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../../DDGI.md
    title: DDGI.md, step 25 and findings log
  - id: blob
    resource: /ddgi/findings/near-field-blob.md
    title: Probes put nearby bounce light in the wrong place
---

# Problem

Probes hold the light at points a probe spacing apart and blend over many
updates. Light that an object close to a surface throws on it (a red ball
next to a white wall) shows as a soft blob around the closest probes and
trails behind the object when it moves. Lower hysteresis and no display
easing make it follow faster but turn every surface blotchy. See
[the finding](/ddgi/findings/near-field-blob.md).

# What was built (2026-10-12, branch `dev-ddgi-quality`)

`rendering/global_illumination/ddgi/near_field_rays` (default on) and
`near_field_distance` (2 probe spacings), with the low resolution apply:

1. The low resolution apply pass writes one cosine-distributed ray per
   block (stratified over 4x4 neighboring blocks, rotated every frame)
   and the probes' irradiance at the block.
2. The ray tracing pipeline traces the rays as it traces probe rays (same
   hit shading: emission, direct light with shadow rays, bounce data),
   up to the range (`RT_PARAM_DDGI_TRACE` 3 or 4).
3. Resolve: hits add the probes' light at the hit (bounce light, as the
   probe bounce pass does). Rays that hit nothing within range take the
   probes' irradiance at the block: open ground gets exactly the probes'
   light, without noise.
4. Temporal accumulation per block, reprojected with the previous camera,
   history matched by depth and normal, clamped to the 5x5 neighborhood
   (mean +- 4 standard errors + 3%), blend at least 0.06.
5. Two passes of an edge-aware 5x5 filter (taps 1 and 2 blocks apart),
   then the result replaces the probes' irradiance in the low resolution
   data (gradients scaled by the same ratio); the upsample is unchanged.

# Results

RTX 3060, test project, Medium:

| | Probes only | Near-field rays |
| --- | --- | --- |
| DDGI ms, room, 1080p | 1.13 | 1.50 (apply 0.40 -> 0.78) |
| Interior flicker std / pixels | 0.05 / 0.00-0.03% | 0.07 / 0.07% |
| Bleed view (static) flicker std | 0.12 | 0.145 |
| GI mean, interior / room | 13.9 / 151.6 | 13.2 / 145.5 |
| Storm game level, GI mean moving / settled | 12.8 / 21.3 | 12.8 / 21.2 |

In the bleed view the red on the wall sits next to the ball, on its
sunlit side, with a red contact tint on the floor (as in the path
tracer), and follows the ball with no trail. With probes only it is a
soft blob around the nearest probes, partly above the ball, that lags.

# Open

- Flicker is still a bit above probes only; more rays per block or a
  variance-guided filter would lower it.
- One view only (XR falls back to the probes).
- The history is reprojected with the camera only; a moving object's own
  surface relies on the clamp.
