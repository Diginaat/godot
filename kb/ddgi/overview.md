---
type: Overview
title: DDGI overview
description: Ray traced irradiance probes for Forward+, written from the DDGI papers, with baking, scrolling cascades and adaptive scheduling.
tags: [ddgi, gi, probes, ray-tracing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../DDGI.md
    title: DDGI.md (working notes, steps, benchmarks)
  - id: majercik19
    resource: https://jcgt.org/published/0008/02/01/
    title: Majercik et al., Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields, JCGT 2019
  - id: majercik21
    resource: https://jcgt.org/published/0010/02/01/
    title: Majercik et al., Scaling Probe-Based Real-Time Dynamic Global Illumination for Production, JCGT 2021
---

# What is built

An independent implementation of DDGI (the RTXGI SDK license doesn't allow
use in this MIT fork).[^majercik19][^majercik21] Details, step table and
benchmarks are in [DDGI.md](../../DDGI.md).[^ddgi-md]

| Part | State |
| --- | --- |
| Probes | Octahedral irradiance (6-8 texels) and distance moments (12-16 texels) per probe, uniform grids in up to 4 cascades |
| Placement | Fixed `DDGIVolume` box, or camera-following scrolling cascades (`infinite_world`) |
| Update | Budgeted scheduler (in view, changing, young probes first) or realtime (every probe every frame); relocation, classification (inside / inactive) |
| Rays | Hardware ray tracing through the path tracer's pipeline; bounce light looked up in a compute pass after the trace |
| Noise | Hysteresis with light-change detection, temporal gradient from fixed rays, guided rays (vMF lobe) |
| Sampling | 8 probes trilinear, backface wrap weight, Chebyshev visibility, weight crush; cascades blended at edges |
| Baking | `DDGIProbeData`; Baked (no ray tracing needed) and Baked + Dynamic |
| Integration | Writes the GI buffer the Forward+ scene shader reads; occludes sky reflections by DDGI over open ambient; lights volumetric fog |

# Known limits (from DDGI.md)

- Light leaks through walls thinner than about a quarter of the probe spacing.
- Uniform grids: detail limited by spacing everywhere; no extra density near geometry.
- Diffuse only; transparent surfaces use the environment ambient light.
- Vulkan ray tracing needed for updates; Baked works without.

# Related

* [Improvement proposals](/ddgi/improvements/index.md)
* [Render graph ordering issue](/ddgi/findings/render-graph-bounce-loss.md)

[^majercik19]: Majercik et al. 2019
[^majercik21]: Majercik et al. 2021
[^ddgi-md]: DDGI.md
