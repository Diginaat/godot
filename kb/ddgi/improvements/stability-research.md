---
type: Research
title: Stable, flicker-free DDGI - what the literature suggests and what this fork already has
description: Web research of 2026-11-27 on temporal stability of probe GI (RTXGI, the 2021 production DDGI paper, DDGI resampling, GI-1.0, adaptive hysteresis), checked against ddgi_update.glsl. Most techniques are already in; perceptual irradiance encoding is the main one missing.
tags: [ddgi, flicker, stability, research, proposals]
status: draft
sources:
  - id: scaling
    title: Majercik, Marrs, Spjut, McGuire, Scaling Probe-Based Real-Time Dynamic Global Illumination for Production, JCGT 10(2), 2021 (arXiv 2009.10796)
    url: https://jcgt.org/published/0010/02/01/paper.pdf
  - id: resampling
    title: Majercik, Mueller, Keller, Nowrouzezahrai, McGuire, Dynamic Diffuse Global Illumination Resampling, CGF 2021 (arXiv 2108.05263)
    url: https://arxiv.org/abs/2108.05263
  - id: rtxgi
    title: RTXGI-DDGI SDK release notes, Probe Variability (v1.3.7), via a third-party mirror
    url: https://git.tdem.in/NVIDIAGameWorks/RTXGI-DDGI/releases
  - id: morgan
    title: McGuire, DDGI optimization notes (hysteresis drop on large changes, rays against hysteresis)
    url: https://github.com/morgan3d/articles/blob/main/2019-04-01-ddgi/optimization.html
  - id: adgi
    title: Datta et al., Adaptive Dynamic Global Illumination (arXiv 2301.05125)
    url: https://arxiv.org/pdf/2301.05125
  - id: gi10
    title: AMD GI-1.0, A Fast Scalable Two-Level Radiance Caching Scheme (GPUOpen 2022, arXiv 2310.19855)
    url: https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf
  - id: giathome
    title: jglrxavpok, We have GI at home (2025, a GI-1.0 style reimplementation)
    url: https://jglrxavpok.github.io/2025/03/18/gi-at-home.html
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# Where the fork stands

DDGI suite on `dev-renderer-next` (`957eff26cf`), 2026-11-27:

| Test | Result |
| --- | --- |
| flicker (static interior) | `flicker_std` 0.104, `flicker_px` 0.00% |
| weather | patchiness outdoor mean 0.204 (max 0.318), interior 0.021; lag after the storm 94 / 82 frames |
| scroll (8 m/s) | mean error 2.71-2.83, 2.5-2.7% of pixels off by more than 16 levels |
| bake | 2.3 s, baked first frames `flicker_std` 0.0 |

A still view no longer flickers. What is left: patchiness while the
weather changes, the lag after a change, error at the scroll edge, and a
moving lamp's trailing light ([moving light](/ddgi/findings/moving-light-trailing.md)).

# Techniques, checked against `ddgi_update.glsl`

| Technique | Source | Here |
| --- | --- | --- |
| Hysteresis blend | DDGI 2019 | Yes |
| Firefly clamp of single rays (hue kept) | scaling | Yes (`ddgi.schedule.z`) |
| Hysteresis lowered on real light changes only | scaling, morgan | Yes: change above two standard errors of the ray mean, confirmed by two updates in the same direction |
| New probes ramp up their hysteresis | thesis via adgi | Yes (`young_limit`) |
| Per-probe convergence measure, fewer updates when converged | rtxgi (Probe Variability) | Partly: per-probe `variability` scales the update rate; nothing stops tracing a converged volume |
| Relocation, classification | scaling | Yes |
| Perceptual (gamma) encoding of irradiance before blending | scaling, RTXGI default gamma 5 | **No**: `mix(result, previous, hysteresis)` in linear |
| Global hysteresis drop on gameplay events (explosion, time of day jump) | morgan | No API; per-probe detection only |
| Jittered lookup / seam dithering | giathome, Enemies | No (cascades blend at their edges) |
| Reservoir resampling of probe and screen samples | resampling | No (path tracer territory, large) |

# Candidates, by value for cost

1. **Perceptual irradiance encoding** (small). Store irradiance as
   `pow(x, 1/gamma)` and blend there; decode when sampling. The paper says
   it speeds convergence in dark areas and dampens the low-frequency
   flicker that single bright rays leave. Cost: the blend of encoded values
   is not the linear mean, so a probe whose rays vary reads darker; it may
   also change the noise-aware change detection, which works in linear.
   Measure with the whole suite: `flicker`, `weather` patchiness and lag,
   and `mean_linear` against the current build (see
   [tuned for speed before checking flicker](/mistakes/tuned-speed-before-stability.md)).
2. **A lighting-event call** (small): something like
   `DDGIVolume.notify_lighting_changed(strength)`, lowering the hysteresis
   for a few updates as the paper's heuristics do (small change: -15% for 4
   frames, large: -50% for 10), for changes the game knows about
   (time-of-day jumps, explosions). Targets the weather lag.
3. **Stop tracing converged volumes** (medium): when the volume's
   variability stays low and nothing moved or changed, skip probe traces
   (RTXGI Probe Variability). A GPU saving, not a stability fix; the
   scheduler already favours changing probes.
4. **Neighbour blend for inside probes** ([proposal](/ddgi/improvements/inside-probe-neighbour-blend.md))
   and **scroll init from the coarser cascade** ([proposal](/ddgi/improvements/scroll-init-from-coarser.md))
   target the patchy and scroll-edge errors.
5. **Screen-space temporal filtering of the apply output** is how
   GI-1.0 and Brixelizer stabilise; this fork removed its screen-space
   near-field history because it left spots that moved with the camera
   ([finding](/ddgi/findings/near-field-camera-spots.md)). With DLSS Ray
   Reconstruction or TAA already accumulating, a second history needs a
   clear failing case first.
