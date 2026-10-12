---
type: Reference
title: O3DE DiffuseProbeGrid gem
description: O3DE's DDGI integration (built on NVIDIA RTXGI); its settings and low resolution composite, and why no code was taken from it.
tags: [research, ddgi, engines]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: gem
    resource: https://github.com/o3de/o3de/tree/development/Gems/DiffuseProbeGrid
    title: O3DE DiffuseProbeGrid gem (Apache-2.0 OR MIT; the RTXGI shaders inside under the NVIDIA RTX-GI License)
---

# What it is

O3DE's probe GI is NVIDIA's RTXGI DDGI wrapped in O3DE passes.[^gem] The
probe shaders (blend, border update, relocation, classification, ray
tracing) ship only as precompiled shaders under the NVIDIA RTX-GI License.
The gem's own source (Apache-2.0 OR MIT) is the C++ passes and three
shaders: a downsample, a composite and a visualization.

# Defaults worth knowing

| Setting | Value | Meaning |
| --- | --- | --- |
| Probe spacing | 2 m | |
| Rays per probe | 288 | |
| Hysteresis | 0.95 | Same as ours |
| Irradiance threshold | 0.2 | A larger change lowers the hysteresis |
| Brightness threshold | 1.0 | Limits how much a probe can brighten in one update (firefly suppression) |
| Irradiance encoding gamma | 5 | As in the reference code |
| Distance exponent | 50 | Sharpness of the distance (visibility) blend |
| Normal / view bias | 0.1 / 0.2 | Ours 0.1 / 0.3 (times the spacing) |
| Emissive multiplier | 1.0 | Per grid: scales how much emissive surfaces feed the GI |

# Low resolution composite

It renders the probe irradiance at a lower resolution, then per pixel takes
the downsampled texel whose normal (dot above 0.99) and depth match, or
the best normal match in a small neighborhood: a nearest pick, no
blending, no directional data. Our
[low resolution apply](/ddgi/improvements/low-res-apply-directional.md)
blends and evaluates a gradient over the normal.

# For this fork

- Nothing copied: the useful parts (the thresholds) are in RTXGI's
  licensed shaders, and the open composite is simpler than ours.
- An emissive multiplier per volume is a cheap knob to add.
- The brightness threshold is the RTXGI answer to firefly probes; the
  emissive lights mode (in progress) removes
  most of their cause instead.

[^gem]: O3DE DiffuseProbeGrid gem
