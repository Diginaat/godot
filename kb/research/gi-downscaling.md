---
type: Reference
title: Global illumination downscaling
description: Computing screen-space GI at 1/4 to 1/32 resolution - depth-normal aware upsampling, downsampled G-buffer for stability instead of temporal filtering, L1 SH per low-res pixel to keep normal-map detail.
tags: [research, gi, performance, upsampling, spherical-harmonics]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: post
    resource: https://frostbone25.github.io/p/adventures-global-illumination-downscaling/
    title: Matos, Global Illumination Downscaling (blog, 2026-01-07)
---

# Summary

- Computing GI at 1/4 resolution (480x270 from 1080p) is about 94% less
  work, but point or bilinear upsampling looks blocky and flickers.[^post]
- **Depth-aware upsampling**: a 9-tap single-pass Gaussian weighted by
  depth difference keeps edges.
- **Depth-normal aware upsampling** adds a normal weight,
  `pow(saturate(dot(highResNormal, lowResNormal)), normalPower)`: clearly
  better edges and normal-map detail.
- **Temporal filtering** at full or low resolution reduces flicker but
  ghosts, depends on frame rate and softens the image.
- **Downsampled G-buffer** (average depth and normals 2x2 down to the GI
  resolution) is the stable option: it averages pixel changes over an
  area, so the low-resolution GI flickers less, with no ghosting. Preferred
  over temporal filtering.
- **L1 SH per low-resolution pixel** (4 RGB coefficients, packed into 3
  RGBA16F targets, written in one compute pass: 3x faster than separate
  passes): the GI is gathered over the whole sphere, and the full-resolution
  normal evaluates it, so normal maps and small detail survive even at
  1/8 to 1/16 resolution.
- Remaining artifacts: slight halos around foreground objects from the
  downsampled buffers, pixelation at the lowest resolutions.
- Recommendations: depth-normal aware upsampling as the minimum, buffer
  downsampling for stability, SH for detail, depth and normal packed in one
  RGBA32 target, compute shaders for multi-target output.

# For this fork

[Low-resolution apply with directional output](/ddgi/improvements/low-res-apply-directional.md):
the DDGI apply pass is about half of the DDGI cost (0.66 of 1.33 ms at
1080p) and its only reduction today is half resolution with softer
edges.

[^post]: Matos, GI downscaling
