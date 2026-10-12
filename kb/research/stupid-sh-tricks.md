---
type: Reference
title: Stupid Spherical Harmonics (SH) Tricks
description: Sloan's SH primer for games - evaluating lighting with SH, ringing and windowing (Hanning, Lanczos, content-sensitive), extracting directional + ambient lights, SH products.
tags: [research, spherical-harmonics, irradiance, ringing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: paper
    resource: https://www.ppsloan.org/publications/StupidSH36.pdf
    title: Sloan, Stupid Spherical Harmonics (SH) Tricks, companion paper to the GDC 2008 lecture (Microsoft)
---

# Summary

- SH of order N has N^2 functions (bands 0 to N-1, 2l+1 functions per
  band). In Cartesian form they are polynomials, which is how shaders
  evaluate them. Zonal harmonics (one per band) are the functions with
  circular symmetry around Z.[^paper]
- **Irradiance**: convolving radiance with the clamped cosine scales each
  band by a constant, so 9 coefficients (order 3) represent irradiance
  well. The paper compares SH with the Ambient Cube basis (HL2): SH gives
  the same result for any light direction, the ambient cube doesn't.
- **Extracting lights**: an SH environment can be approximated by 1-3
  directional lights plus ambient (least squares), for fast shading.
- **Ringing** (Gibbs) comes from truncating the series: negative lobes and
  rings, worse with HDR or bright lights, and colored fringes when one
  color channel rings more than the others.
  - **Windowing** scales band l by a sigma factor: Lanczos (sinc) or
    Hanning (raised cosine). It trades ringing for blur.
  - Rarely needed for irradiance maps; scenes with a lot of normal
    variation hide ringing.
  - **Content-sensitive windowing**: blend between windowed and unwindowed
    light depending on the angle between the reflection vector and the
    dominant light direction, keeping sharp highlights without rings.
- SH products and their cost, for multiplying light by visibility.

# For this fork

- Our probes store irradiance in octahedral maps, not SH, so ringing
  doesn't occur there.
- It matters if a directional low-resolution GI output is added
  ([low-resolution apply](/ddgi/improvements/low-res-apply-directional.md)):
  L1 SH of the GI at low resolution can ring negative in high-contrast
  light. Clamp or window it, using one window for all color channels (see
  the de-ringing in [CoD: Infinite Warfare](/research/cod-iw-precomputed-lighting.md)).

[^paper]: Sloan, Stupid SH Tricks
