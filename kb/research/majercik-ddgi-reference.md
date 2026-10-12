---
type: Reference
title: DDGI papers and reference code (Majercik et al. 2019, 2021)
description: The original DDGI paper and its scaling follow-up, with their reference shaders, compared line by line with this fork's DDGI; what differs and what that means.
tags: [research, ddgi, reference]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi2019
    resource: https://jcgt.org/published/0008/02/01/
    title: Majercik, Guertin, Nowrouzezahrai, McGuire, Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields, JCGT 8(2), 2019
  - id: ddgi2021
    resource: https://jcgt.org/published/0010/02/01/
    title: Majercik, Marrs, Spjut, McGuire, Scaling Probe-Based Real-Time Dynamic Global Illumination for Production, JCGT 10(2), 2021
---

# What the reference code does (2021 supplement)

- **Sampling** (`DDGIVolume.glsl`): the same weights as this fork:[^ddgi2021]
  - backface wrap `((dot + 1) / 2)^2 + 0.2`;
  - Chebyshev visibility cubed;
  - crush below 0.2 (`w * w^2 / 0.04`);
  - trilinear weights;
  - blending in square root space.
  Differences: the visibility weight never goes below 0.05
  (`max(0.05, chebyshev)`, so a point that no probe sees still gets a
  fallback blend), and the bias offset is scaled by one `selfShadowBias`.
- **Probe update** (`DDGIVolume_updateProbes.glc`): irradiance is stored and
  averaged gamma-encoded (gamma 5, `pow(x, 1/5)`), not linear. A change of
  more than 0.25 (in that space) lowers the hysteresis by 0.15, more than
  0.8 sets it to 0. Averaging gamma-encoded values squashes rare bright ray
  hits (a small emitter seen by one ray now and then) before they enter the
  average: smooth, but biased darker where the light comes from small,
  bright sources.
- Relocation, classification and scrolling volumes as in the 2021 paper;
  this fork has all three.

# Compared with this fork

| | Reference | This fork |
| --- | --- | --- |
| Temporal average | Gamma 5 space, threshold drops | Linear, with a noise estimate per texel, confirmed changes, display smoothing |
| Visibility floor | 0.05 | None (weights can reach 0 before the crush) |
| Small bright emitters | Squashed by the gamma average | Noisy in the probes; emissive lights mode (2026-10-12) takes them out of the probes and lights surfaces directly |

# For this fork

- The 0.05 visibility floor is a candidate against dark wedges between
  probes; it needs the same test suite (flicker, leaks) before use.
- Gamma-space averaging isn't adopted: it darkens rooms lit by small
  sources (the same bias DDGI.md found for value-adaptive blending).

[^ddgi2021]: Majercik et al., Scaling Probe-Based Real-Time Dynamic GI, 2021
