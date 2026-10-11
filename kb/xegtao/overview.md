---
type: Overview
title: XeGTAO ambient occlusion
description: Intel's XeGTAO ported to GLSL compute, set on the Environment, replacing SSAO as producer of the Forward+ AO texture.
tags: [xegtao, ambient-occlusion, screen-space, environment]
status: draft
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: xegtao
    resource: https://github.com/GameTechDev/XeGTAO
    title: Intel XeGTAO (MIT)
  - id: gtao
    resource: https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    title: Jimenez et al., Practical Real-Time Strategies for Accurate Indirect Occlusion, 2016
---

# State

Branch `dev-ddgi-ao` (commit `84d0566785`), not merged. Works and tested;
benchmarks at 1440p and 4K, the Auto resolution policy and a D3D12 run are
open.[^xegtao][^gtao]

# Design

- Settings on the Environment, group XeGTAO next to SSAO (`xegtao_*`);
  replaces SSAO while on. First built on `DDGIVolume`, moved on request:
  AO is a camera-wide screen effect, not part of DDGI.
- Writes the texture the scene shader already reads as SSAO, so AO is
  applied once, to ambient light only (no double AO, emission and direct
  light untouched).
- Passes: depth MIP chain (R16F, 5 MIPs), horizon search (Intel's presets:
  slices x steps Low 1x2, Medium 2x2, High 3x3, Ultra 9x3), edge-aware
  denoise (1 pass with TAA, 2 without), depth-guided upscale at half
  resolution. One compute list per pass (GPU timestamps).
- Runs after the GI processing: see
  [the render graph finding](/ddgi/findings/render-graph-bounce-loss.md).
  Bent normals are therefore a computed and debug output only.

# Measurements (RTX 3060, 1920x1080, room view, Medium DDGI)

| Setting | AO GPU ms |
| --- | --- |
| Godot SSAO | 0.19 |
| XeGTAO Low | 0.38 |
| XeGTAO Medium | 0.47 |
| XeGTAO High | 0.68 |
| XeGTAO Ultra | 1.48 |
| XeGTAO High, half resolution | about 0.2 |
| Bent normals | +0.2 |

Measured 2026-10-11 with XeGTAO before the GI pass (same shader work).
DDGI is unaffected with XeGTAO after GI (GI buffer 13.9 with and without,
600 frames).

# Lessons

- Material samplers carry the mipmap LOD bias (TAA, upscalers); explicit
  `textureLod()` depth MIP picks need their own sampler.
- `RENDER_TIMESTAMP` fails inside a compute list that already dispatched.

[^xegtao]: Intel XeGTAO
[^gtao]: Jimenez et al. 2016
