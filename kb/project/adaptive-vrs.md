---
type: Finding
title: Content-adaptive variable rate shading (VRS Adaptive)
description: GPU picks each tile's shading rate from the previous frame's contrast; default for the main viewport; 2-5% on the valley, 3.6x on an expensive-shader floor, about 47 dB PSNR.
tags: [vrs, shading, gpu, rendering]
status: stable
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What

`Viewport.VRS_ADAPTIVE` / `RenderingServer.VIEWPORT_VRS_ADAPTIVE`, threshold
`rendering/vrs/adaptive_threshold` (0.1). `rendering/vrs/mode` defaulted to
Adaptive (3) in this build from 2026-11-24; since 2026-11-27 it is Disabled
again (D042), so a project opts in. SubViewports keep their own `vrs_mode`
(Disabled by default). GPUs without attachment VRS ignore it.

2026-11-27, `light_bench` at 1080p on an RTX 5070, GPU frame with Adaptive
against Disabled: Vulkan 0.243 against 0.238 ms with no lights (the opaque
pass 0.036 against 0.095 ms); D3D12 0.433 against 0.228 ms, with the same
opaque pass time either way (the rate image does not seem to take effect
there; under investigation).

# How

`VRS::update_vrs_texture_adaptive()` (`effects/vrs.cpp`), kernel
`shaders/effects/vrs_adaptive.glsl`, called from
`RendererSceneRenderRD::_update_vrs()` before the colour passes. One
workgroup per shading-rate texel: it loads the tile plus a 4 px margin (for
motion) from the internal colour buffer, which still holds the previous
frame's HDR image (the render target can be cleared before 3D draws, so it
is not used). Luma is compressed (`sqrt(L/(1+L))`), then per axis:
`half_error = 0.5 * mix(rms_difference, max_difference, 0.5)`; quarter rate
errs 2.13 times that (Yang et al. 2019). A tile goes 2x (or 4x) on an axis
when the error is below `threshold * (mean_luma + 0.05)`. 4x1/1x4 are not
allowed. The R8 rate image and the RG density map format are both written.
Tiles larger than 32 px, no internal texture, or threshold 0 write full rate.

The max-difference term matters: with the mean alone, one silhouette through
a flat tile let it go coarse, and the coarse sample shaded both sides of the
edge (valley view 0: 35.9 dB at 0.1; with the mix 46.9 dB).

# Measurements (2026-11-24, Linux, RTX 3060, Vulkan, 1600x900, time frozen)

Fixture `misc/large_world_test_project/vrs_adaptive.tscn` (`-- --view-time=T`,
`--vrs-shots=DIR`), viewport GPU time against VRS off, PSNR against VRS off:

| Valley view | 0.05 | 0.10 (default) | 0.20 |
| --- | --- | --- | --- |
| 0 | 99%, 53.9 dB | 97%, 46.9 dB | 91%, 38.6 dB |
| 27 | 99%, 50.5 dB | 98%, 47.1 dB | 96%, 42.4 dB |

The valley's materials are cheap, so VRS saves little. A smooth floor with
a 2000-iteration fragment shader at 1080p: 7.0 ms to 1.94 ms (28%), the
high-contrast checker wall stays full rate.

# Effects on tests

Rates depend on the previous frame, so images are history-dependent like
TAA. Fixtures that compare captures pixel for pixel pin
`vrs_mode = VRS_DISABLED` (`hzb_two_pass`, `multimesh_occlusion`,
`clod_raster`). The DDGI flicker measure is unchanged (0.00% pixels).
Note (2026-11-27): that figure doesn't match the interior flicker test of
the same period, which gave 8-16% with and without VRS because of a render
graph hazard (fixed since; see
[render-graph-bounce-loss](/ddgi/findings/render-graph-bounce-loss.md)).
It was probably a shorter run; treat it as unverified.

# Not done

No motion reprojection (the margin covers a few pixels of motion); not
tried with TAA/DLSS jitter for shimmer; Mobile renderer untested.
