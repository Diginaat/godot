---
type: Finding
title: Attachment VRS on D3D12 did nothing, then used the wrong tile size
description: Two D3D12 driver bugs (also in upstream Godot master 94fc09c62d). The shading rate image was taken as a plain colour attachment and never bound (RSSetShadingRateImage), so every VRS mode shaded at full rate. The driver also reported a maximum texel size of 8 against a 16 pixel hardware tile, so RD built the rate image for 8 pixel tiles. Both fixed. The adaptive VRS quality now matches Vulkan (47.5-47.9 dB at threshold 0.1).
tags: [vrs, d3d12, driver, correctness, upstream-bug]
status: stable
sources:
  - id: fixture
    title: misc/large_world_test_project/vrs_adaptive.tscn, 1920x1080, RTX 5070, 2026-11-27
  - id: bench
    title: devtools/test/light_bench.ps1 with rendering/vrs/mode in override.cfg, 2026-11-27
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Bugs

Both are in `drivers/d3d12/rendering_device_driver_d3d12.cpp`.

1. **The rate image was never given to the pass.** `_framebuffer_create()`
   checked `ALLOW_RENDER_TARGET` before the subpass's shading rate
   reference. The VRS texture allows render target use (texture-mode VRS
   draws into it), so it was filed as a colour attachment and
   `vrs_attachment` stayed empty. Even with it set,
   `command_begin_render_pass()` only set the combiners
   (`RSSetShadingRate(1X1, {PASSTHROUGH, OVERRIDE})`) and never called
   `RSSetShadingRateImage()`. Result: no VRS mode had any effect on D3D12.
   The opaque pass took the same time with VRS on or off, and Adaptive
   cost 0.205 ms for nothing.
2. **Wrong tile size.** `fsr_capabilities.max_texel_size` was (8, 8)
   while `min_texel_size` is `ShadingRateImageTileSize` (16 here). RD
   clamps its wanted 16 into [16, 8] and gets 8. The rate image was then
   twice as fine as the GPU reads it, and rates landed on the wrong
   pixels. Once the image was bound, Adaptive at threshold 0.05 gave
   27-41 dB.

# Fix

- The VRS attachment is recognized first and kept in the attachment list,
  which other code indexes like the render pass's attachments.
  Dropping it from the list crashed: index out of bounds.
- `RSSetShadingRateImage(resource)` is called after the combiners. RD's
  rate values `(log2 w << 2) | log2 h` are the `D3D12_SHADING_RATE` values.
- `max_texel_size = min_texel_size` (D3D12 has one tile size).

# Measured (1920x1080, RTX 5070)

`vrs_adaptive` fixture, threshold 0.1, PSNR against VRS off:

| Driver | Valley view 0 | Valley view 27 |
| --- | --- | --- |
| Vulkan | 47.7 dB, 95% GPU time | 47.6 dB, 98% |
| D3D12 before | 39.1 dB (fails) | 27.2 dB (fails) |
| D3D12 after | 47.9 dB, 98% | 47.5 dB, 99% |

`light_bench`, Adaptive against off, ms (opaque pass in brackets):

| Driver | 0 lights | 512 lights | 4096 lights (512 drawn) |
| --- | --- | --- | --- |
| Vulkan | 0.245 (0.035) vs 0.239 (0.095) | 0.453 (0.196) vs 0.417 (0.226) | 0.507 (0.233) vs 0.512 (0.304) |
| D3D12 | 0.254 (0.034) vs 0.230 (0.091) | 0.418 (0.159) vs 0.362 (0.183) | 0.451 (0.176) vs 0.430 (0.235) |

The adaptive kernel costs 0.063 ms (Vulkan) and 0.076 ms (D3D12). Its
reduction now uses subgroup sums: two barriers per workgroup instead of
eight. With cheap materials the kernel costs about what VRS saves, which
is one reason VRS is off by default (D042).
