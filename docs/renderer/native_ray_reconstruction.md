# Native ray reconstruction (path tracer denoiser)

Working notes and documentation for the native, cross-vendor denoiser of
this build. This file is the shared memory for humans and agents working on
it: read it before starting, and update the step table and the findings log
as you go. Work happens on the `dev-rrdenoiser` branch (from `dev-ddgi`).

## Goal

A spatiotemporal denoiser for the path tracer that runs on any GPU that runs
the path tracer, built only on `RenderingDevice` compute shaders. It is an
alternative to DLSS Ray Reconstruction, which needs an NVIDIA GPU and the
Streamline DLLs (which public releases don't ship). No new dependency: no
DLSS, Streamline, OptiX, CUDA or neural runtime. All code is MIT licensed
with the engine.

It is written from published papers, not from SDK source:

- C. Schied et al. "Spatiotemporal Variance-Guided Filtering: Real-Time
  Reconstruction for Path-Traced Global Illumination." HPG 2017 (SVGF).
- C. Schied, C. Peters, C. Dachsbacher. "Gradient Estimation for Real-Time
  Adaptive Temporal Filtering." HPG 2018 (A-SVGF).
- Public design notes of AMD FidelityFX Denoiser (reflections, shadows;
  MIT licensed) for reflection reprojection and history clamping ideas.

NVIDIA NRD is a comparison target only. No NRD source, binary or shader is
used.

### Other research and code, and what may be used

Checked on 2026-10-10. Only permissive code may be ported, with its notice
kept (`COPYRIGHT.txt`, `THIRD_PARTY_LICENSES.md`); GPL code can only inform an
independent implementation from the paper.

| Work | Code license | Use here |
| --- | --- | --- |
| A-SVGF: Schied, Peters, Dachsbacher, "Gradient Estimation for Real-Time Adaptive Temporal Filtering", HPG 2018 (cg.ivd.kit.edu/atf.php) | BSD-3-Clause (reference shaders, Falcor/OpenGL) | Step 5b: temporal gradients from re-shading forward projected samples with last frame's random seed, in place of the heuristic change detection. The authors ask for a short notice when it ships in a product. |
| merian (github.com/LDAP/merian): Vulkan framework with SVGF, accumulation with percentile firefly clamping and adaptive alpha, TAA, SSMM guiding, hashed irradiance cache | BSD-3-Clause | Ideas and code may be ported: firefly clamp at median + k x interquartile range of a screen tile (robust to the heavy tails that made a 4-sigma clamp useless), history shortening when the history leaves the tile's interquartile band, stochastic bilinear reprojection, a wider search after disocclusion |
| Real-Time Markov Chain Path Guiding (Alber, Hanika, Dachsbacher, I3D 2025), in merian-quake (github.com/LDAP/merian-quake) | GPL-2.0 (built on Quake) | Not portable. Path guiding lowers the noise of the input (vMF mixtures in adaptive and hash grids); an independent implementation from the paper is a possible later step for the path tracer, separate from the denoiser |
| Optimized and Aligned Anisotropic Monte Carlo Sampling Patterns (Werner, Hanika, Dachsbacher, EGSR 2026), github.com/MircoWerner/AnisotropicSampling | GPL-3.0 | Not portable. Targets offline sampling; at 1 spp per frame the matching real-time technique is spatiotemporal blue noise sampling (later step) |

## Audit (step 1)

Audited on 2026-10-09 at commit `dc1790d9af` (`dev-ddgi`). GPU: RTX 3060,
driver 616.92, Vulkan. No AMD or Intel GPU was available.

### Which signals are noisy

| Mode | Per-pixel stochastic signal | Existing denoising |
| --- | --- | --- |
| Path tracer (`Environment.pathtracing_enabled`) | Yes: the whole image. 1-16 spp, all lighting (direct and indirect, diffuse and specular, mesh lights, glass) in one rgba32f image | DLSS RR only (`pathtracing_denoiser = 1`, NVIDIA, needs the DLSS scaling mode and Streamline DLLs). Otherwise none; FSR2 and TAA smear the noise a little |
| Forward+ with DDGI | No. DDGI apply interpolates probes deterministically (no random numbers in `ddgi_apply.glsl`); the noise is in the probes and is filtered by the probe hysteresis | Probe hysteresis, adaptive per probe |
| Forward+ shadows and reflections | No ray traced shadows or reflections exist in raster mode: shadow maps, reflection probes, SSR | n/a |

Consequences for the plan:

- The denoiser's job is the path tracer image. That image contains the
  shadows (NEE shadow rays), reflections and GI in one signal, so the
  diffuse/specular split below covers them; there is no separate shadow
  signal to give a dedicated shadow denoiser.
- A dedicated ray traced shadow denoiser (FidelityFX-shadows style tile
  classification) only makes sense together with a new feature: ray traced
  shadows in raster mode. That's outside this work for now (see step 11).
- SVGF must **not** run over DDGI output: it isn't noisy per pixel, and
  filtering it again would only add lag. DDGI work stays in DDGI.md (its
  steps 9 and 10); the denoiser and DDGI never touch the same signal, since
  the path tracer turns DDGI off.

### Path tracer frame (`RenderForwardClusteredPT::_render_scene`)

1. Setup, lights, volumetric fog, `build_tlas()` (shared with DDGI).
2. One `traceRays` at internal resolution (`Pathtracer` timestamp). Raygen
   (`scene_raytracing_raygen.glsl`) loops samples and bounces; closest hit
   (`shade_and_bounce()`) adds emission, NEE and picks the next lobe.
3. Outputs: radiance (binding 0, rgba32f, averaged over spp), NDC depth
   (binding 15, r32f, sample 0, copied to the D32 depth buffer), motion
   vectors (binding 28, rg16f), and with DLSS RR the guides: diffuse albedo,
   specular albedo, normal + roughness, specular hit distance (bindings
   9-12, written at the primary hit of sample 0).
4. `copy_output_texture()` to the internal color texture, transparent raster
   overlay, then `_render_3d_upscaling()` (FSR2, DLSS(+RR), MetalFX or TAA),
   tonemap.

The radiance is already composited: diffuse and specular, direct and
indirect, primary emission and fog are summed. To denoise properly the
raygen has to output them separately (step 2).

### Motion vectors, depth, jitter

- Motion vectors: `prev_uv - curr_uv` in UV units, from unjittered
  view-projections (`RaytracingParams.prev/curr_vp_unjittered`). Same
  convention as raster (`scene_forward_clustered.glsl`, motion vector store).
  Object motion: per-instance previous transforms, skinned/displaced previous
  vertex buffers, procedural deltas. Sky pixels get camera-only motion.
- Depth: NDC depth of the primary hit, 0 for sky (reverse Z).
- Jitter has exactly one owner: the viewport sets a Halton jitter only when
  TAA or a temporal upscaler is on (`jitter_phase_count`), and it reaches the
  path tracer through the jittered `inv_projection_matrix` in the raygen.
  Native resolution without TAA has no jitter. No double jitter found.
- Previous surface positions: not stored. The denoiser keeps last frame's
  depth and normal itself (step 3).
- Mesh/material IDs: not available as a buffer; `gl_InstanceCustomIndexEXT`
  at the primary hit can be written if depth/normal tests turn out too weak.

### Temporal effects that exist

| Effect | History | Reset |
| --- | --- | --- |
| TAA (`effects/taa.cpp`) | Color + velocity | None explicit |
| FSR2 (`effects/fsr2.cpp`) | Internal | `reset_accumulation = false` always (FIXME in `_render_3d_upscaling`) |
| DLSS / DLSS RR | Internal | Same FIXME |
| DDGI | Probe atlases with hysteresis | Scroll slabs, full reset on jumps |
| Volumetric fog | Froxel reprojection | Internal |

Camera cuts are never reported to any upscaler. Step 8 adds a cut detector
for both the denoiser and FSR2/DLSS.

### Resource lifetime

`RenderSceneBuffersRD::configure()` calls `cleanup()`, which frees every
named texture and custom data. A resize, MSAA or scaling mode change
therefore drops the denoiser history automatically if it lives in the render
buffers. The path tracer already frees its DLSS RR buffers when path tracing
turns off for a view; the denoiser does the same.

### Capabilities

- Path tracing needs `SUPPORTS_RAYTRACING_PIPELINE` (Vulkan). D3D12 has no ray
  tracing in this fork. Without it there is no noisy signal, so the denoiser
  is never used; nothing to fall back from.
- The denoiser itself needs compute shaders, storage images (rgba16f, r32f,
  rg16f) and nothing else: no subgroup operations, no FP16 arithmetic, no
  vendor extensions. Every GPU that can run the path tracer can run it.

### Baseline (before any change)

RTX 3060, `misc/ddgi_test_project`, `room` view, 1 spp, 3 bounces, no
denoiser, native resolution, `--gpu-profile`, 120 warm-up + 200 measured
frames. All times in ms.

| Resolution | Frame GPU | Path tracer | AS | VRAM MB |
| --- | --- | --- | --- | --- |
| 1920x1080 | 15.45 | 14.83 | 0.10 | 160 |
| 2560x1440 | 27.33 | 26.49 | 0.10 | 237 |
| 3840x2160 | 60.93 | 59.53 | 0.10 | 439 |

For comparison, raster + DDGI Medium at 2560x1440: 3.80 ms frame, DDGI 1.73.

Image: at 1 spp the room is unusable without a denoiser (salt-and-pepper
noise everywhere, black speckles in direct light shadows); FSR2 at 0.67
scale only turns the noise into larger blotches. The path tracer at 1 spp
already costs 15 ms at 1080p on this GPU, so the denoiser target is a small
fraction of that; the "below 2 ms at 1440p on an RTX 5070" target maps to
about 3-4 ms on the RTX 3060.

## Design

```
traceRays (1 spp)                     compute, internal resolution
  diffuse radiance / diffuse albedo ─┐
  specular radiance / spec albedo   ─┤  1 reproject + temporal accumulate (radiance, moments, history length)
  primary emission, sky, fog (clean)─┤  2 variance estimate (spatial fallback for short history)
  normal+roughness, depth, motion,  ─┤  3 a-trous x N (diffuse + specular in one pass, shared guide reads)
  spec hit distance                  ┘  4 compose: diff*albedo + spec*albedo + emission  -> internal color
                                        -> transparent overlay -> FSR2/TAA/DLSS SR -> tonemap
```

- Demodulation: diffuse radiance is divided by the diffuse albedo and
  specular radiance by the pre-integrated specular albedo, so texture detail
  never goes through the filter (sharp textures), and is multiplied back in
  the compose pass.
- History: ping-pong textures in render-buffer scope `native_rr`. Validity
  per pixel from reprojected depth (plane distance), normal, and roughness;
  a failed test means history length 0 and a spatial-only estimate.
- Specular: history length and kernel follow roughness; mirror-like pixels
  reproject along the reflected virtual hit point (hit distance) instead of
  the surface motion, and keep a short history.
- Anti-ghosting: neighborhood clipping of a fast history in YCoCg, history
  length capped by a confidence from luminance change (A-SVGF style
  gradients if needed), firefly clamp before accumulation, NaN/Inf guards.
- Upscalers: the denoiser always runs at internal resolution before the
  transparent overlay and FSR2/TAA/DLSS SR. Its output keeps the jitter, so
  the upscaler still resolves sub-pixel detail. Exactly one upscaler, as
  today.

## Steps

| # | Step | Status |
| --- | --- | --- |
| 1 | Audit: signals, render graph, motion vectors, jitter, history, capabilities; baseline timings and images | Done |
| 2 | Path tracer signal split: diffuse and specular radiance, clean primary emission/sky/fog, guide buffers for the native denoiser; pass-through compose must match today's image | Done |
| 3 | Core SVGF: `PT_DENOISER_NATIVE`, history resources, reprojection with depth/normal tests, moments, variance, a-trous, compose, timestamps | Done |
| 4 | Specular reconstruction: roughness-aware history and kernel, hit distance, virtual-hit reprojection for glossy surfaces | Done |
| 5 | Anti-ghosting: history clipping, confidence, firefly clamp, NaN guards, disocclusion fallback | Done (heuristic change detection; see 5b) |
| 5b | A-SVGF temporal gradients (Schied et al. 2018) in place of the heuristic change detection | Done |
| 6 | Test harness: accumulated reference, metrics (error vs reference, temporal flicker, ghost trails, edge sharpness), moving scenes, thin geometry, mirrors, moving emitters | Done |
| 7 | Settings and debug views: `rendering/ray_reconstruction/*`, presets measured against each other, debug views, editor exposure | Open |
| 8 | History invalidation and upscaler integration: camera cuts (also FSR2/DLSS reset), resize, mode switches, viewport create/destroy; FSR2/TAA/DLSS SR combinations; zero-jitter debug | Open |
| 9 | DDGI review: probe validity and generation, transitions, whether a screen-space pass helps (only if measured) | Open |
| 10 | Performance: per-pass timing, half-resolution diffuse, adaptive a-trous iterations, FP16 storage, pass fusion; 1080p/1440p/4K, native and upscaled | Open |
| 11 | Optional, later: ray traced shadows/reflections for raster mode with their own denoisers | Not planned yet |
| 11b | Optional, later: lower input noise in the path tracer: spatiotemporal blue noise sample sequences; path guiding (independent implementation of Markov chain path guiding) | Not planned yet |
| 12 | Documentation, cleanup, merge into `dev` | Open |

## Signal split (step 2)

`Environment.pathtracing_denoiser = Native` (`RenderingServer.PT_DENOISER_NATIVE`,
value 2) sets `RT_FLAG_NATIVE_RR_ENABLED`, which compiles the path tracer with
`NATIVE_RR_ENABLED`:

| Output | Binding | Format | Contents |
| --- | --- | --- | --- |
| `image` | 0 | rgba16f | Clean part: primary emission, fog in-scatter, sky (not denoised) |
| `rr_diffuse` | 40 | rgba16f | Diffuse radiance: diffuse part of direct light at the primary hit, plus everything a diffuse primary bounce brought back |
| `rr_specular` | 41 | rgba16f | Specular radiance (specular direct light, specular bounce, refraction); a = hit distance of the specular bounce (10000 on a miss, -1 when no sample took it) |
| `rr_guide` | 42 | rgba32ui | x diffuse albedo, y specular albedo (unorm8), z octahedral normal (unorm16 x2), w roughness (unorm16) and flags (bit 16: transmissive) |

How the split works: the primary hit (`shade_and_bounce()`,
`refract_and_bounce()`) stores its clean part and the specular part of its
direct light (`lights_direct_specular`, set by
`lights_evaluate_direct_lighting()`) in a payload extension
(`PathPayload.rr_primary`, only in the native variant) and sets payload bit 29.
Raygen then sorts each sample: clean part, diffuse direct light (the rest of
the primary hit's radiance), and the indirect light by the lobe the primary
hit sampled (diffuse bounce counter). Refraction counts as specular.
The parts add up to exactly the old radiance; no random numbers are used
differently, so the image without denoising is unchanged.

Demodulation (dividing by the albedos) is left to the denoiser, which reads
the albedos from the guide. The guides are written by sample 0 only.

Debug views (`rendering/ray_reconstruction/debug_mode`): 1 clean part, 2
diffuse signal, 3 specular signal.

## Core filter (step 3)

`RendererRD::RayReconstruction` (`servers/rendering/renderer_rd/effects/ray_reconstruction.*`,
shader `shaders/effects/ray_reconstruction.glsl`) runs after the trace, at
internal resolution, and writes the internal color texture in place of the
plain copy. Passes (timestamps in brackets):

1. **Temporal** (`RR Temporal`): demodulates (diffuse / diffuse albedo,
   specular / specular albedo, albedos clamped to 1/255 so it's exactly
   invertible), reprojects with the motion vector, checks the four bilinear
   taps of last frame's surface (linear depth within 3% of where the camera
   motion puts the point, normals within about 25 degrees), and blends with
   1 / history length. History caps: 32 frames diffuse, 24 specular (2 for
   mirror-like surfaces until step 4), 8 for the luminance moments. Writes
   this frame's surface record (linear depth, normal, roughness) for the next
   frame.
2. **Variance** (`RR Variance`): luminance variance from the temporal
   moments, or from a 5x5 neighborhood (same geometry only) while the history
   is shorter than 4 frames. Divided by the history length: the filter works
   on the accumulated mean, so converged pixels keep their detail.
3. **A-trous** (`RR Filter 0`-`4`): five 3x3 iterations with steps 1, 2, 4,
   8, 16. Weights: distance to the center's tangent plane (relative to the
   pixel footprint), normal similarity (power 128), luminance difference
   relative to 4 standard deviations. Specular uses the same weights plus
   roughness similarity, and isn't filtered at all where it is mirror-like
   (iteration i filters only roughness above 0.02 * 2^i). Iteration 0 writes
   back into the history (as in SVGF); iteration 4 remodulates and adds the
   clean part.

History lives in render-buffer scope `native_rr_history` (two copies, swapped
every frame); it is dropped with the render buffers (resize) and when the
denoiser or path tracing is turned off.

Debug views (`rendering/ray_reconstruction/debug_mode`): 1 clean part, 2
diffuse (denoised, with albedo), 3 specular, 4 no denoising, 5 history length
(heatmap, blue = new, red = full), 6 variance (red diffuse, green specular),
7 split screen (left not denoised).

Measured (RTX 3060, room view, 1 spp, native resolution, ms):

| Resolution | Path tracer, no denoiser | Path tracer, native variant | Temporal | Variance | Filter (5 it.) | Denoiser total |
| --- | --- | --- | --- | --- | --- | --- |
| 1920x1080 | 15.4 | 16.7-17.3 | 0.52 | 0.29 | 3.3 | 4.1 |
| 2560x1440 | 26.5 | 32.4 | 0.97 | 0.51 | 6.5 | 8.0 |
| 3840x2160 | 59.5 | 69.1 | 1.89 | 1.06 | 11.9 | 14.8 |

VRAM at 1080p: +270 MB (inputs 66 MB, history and filter textures 199 MB).
Both too high; step 10 has the work: each a-trous iteration costs about 0.65
ms at 1080p even where it skips its neighbors, which points at bandwidth
(about 40 bytes read and written per pixel per pass), and the native path
tracer variant costs 8-13% more than the plain one (bigger payload).

## Specular and glass (step 4)

- **Mirror hit distance**: the primary hit of sample 0 traces one ray query
  along the perfect reflection on surfaces with roughness below 0.25 (the
  same query the DLSS RR guide uses) and stores the distance in the guide
  (`w` = half2(roughness, hit distance)). Rougher surfaces use the sampled
  distance of the specular bounce (`rr_specular.a`). Both are accumulated in
  a history of their own (`specular_hit_*`, r16f), so a frame without a
  specular sample keeps the old value.
- **Virtual-image reprojection**: a reflection on a smooth surface moves like
  the virtual image of the hit point (hit distance behind the surface along
  the view ray), not like the surface. The temporal pass projects that point
  into the previous frame (unjittered projections), and accepts taps that lie
  on the same plane in the current view (previous linear depth rebuilt with
  last frame's view ray, moved into the current view). It blends to the
  surface motion as roughness goes from 0 to 0.4. When the virtual tap fails,
  the specular history restarts.
- **History**: mirror-like 8 frames rising to 24 at roughness 0.4; glass 16.
- **Kernel radius**: the specular a-trous iterations stop at the blur radius
  of the reflection lobe: hit distance x roughness^2 (GGX alpha as an angle)
  seen from depth + hit distance away, in pixels. Mirror-like and contact
  reflections stay sharp, rough and far ones get the full kernel. Glass gets
  at least 4 pixels.
- **Glass**: refractive and alpha blended surfaces write the transmissive
  flag (specular albedo alpha) and a specular albedo of 1. Their primary hit
  writes one guide for both outcomes of the alpha choice; before, sample 0
  picked opaque or refracted at random, the albedo flipped between frames and
  the history was remodulated with the wrong one (dark dots). With the native
  denoiser, primary rays always stop at alpha blended surfaces (any hit lets
  them through) and the closest hit either shades the surface (probability
  alpha) or passes straight through (`transmit_and_bounce()`, counted as
  specular). Without the denoiser alpha blend is unchanged.

## Test suite (step 6)

[`misc/denoiser_test_project/`](../../misc/denoiser_test_project/) builds eight
scenes from code. Every view is a pure function of the simulation time (fixed
1/60 s steps while capturing), so separate runs line up frame by frame.

| View | Tests |
| --- | --- |
| `cornell` | Box lit by an emissive ceiling panel (and the sun through the open front), still camera |
| `pan` | Pillars behind a railing of 2.5 cm bars, fast camera pan (disocclusion, thin geometry) |
| `emissive` | Dark room, a bright emissive ball circling (ghost trails of moving light) |
| `skinned` | Three bending skinned tentacles (deformed BLAS, motion vectors), orbiting camera |
| `thin` | 1.5 cm fence bars and alpha scissor leaves, strafing camera |
| `mirror` | Mirror and glossy floors, rough metal spheres, a moving box, orbiting camera |
| `lights` | Two moving colored omni lights and a sweeping spot light (moving shadows) |
| `dark` | Dark room, one small bright light and tiny emitters (high contrast) |

```
godot --path misc/denoiser_test_project -- --view=mirror                  # interactive: 1-8 views, R denoiser, V debug view, F fly camera (--fly starts in it)
godot --path misc/denoiser_test_project -- --view=pan --tonemap=linear --sequence=30 --out=out/denoised
godot --path misc/denoiser_test_project -- --view=pan --tonemap=linear --sequence=30 --reference=256 --out=out/reference
python misc/denoiser_test_project/metrics.py out/denoised out/reference --prefix=pan
python misc/denoiser_test_project/run_suite.py --godot=<editor console exe> --out=<dir> [--keep-reference]
godot --gpu-profile --path misc/denoiser_test_project -- --view=cornell --res=2560x1440 --bench=200
```

Arguments (after `--`): `--view=`, `--res=WxH` (offscreen viewport, default
1280x720), `--spp=`, `--bounces=`, `--denoiser=0|1|2` (default 2),
`--rr_debug=0..8`, `--pt_debug=` (path tracer debug view), `--tonemap=linear`
with `--exposure=` (measurements), `--scale3d=` and `--scale=`, `--taa=1`,
`--frames=N` warm-up, `--shot=`, `--sequence=N --out=dir`, `--reference=K`
(each sequence frame is the average of K frames with the scene frozen, via
the denoiser's Reference debug mode), `--bench=N`, `--teleport=N` (camera cut
every N frames), `--resize_at=F:WxH`, `--scale_at=F:MODE`, `--cycles=N` (create
and free N extra path traced viewports first).

**Reference mode** (`rendering/ray_reconstruction/debug_mode` = 8): the
denoiser is replaced by a running average of the path tracer's image in fp32,
restarted when the camera moves or the mode is entered. With the scene frozen
it converges to the noise-free image (256 frames at 1 spp still leave visible
noise next to small bright lights, as in the cornell and dark views).

**Metrics** (`metrics.py`, numpy and Pillow only), against the reference, on
linear values: `rmse`; `relmse` (relative, weighs dark areas); `ssim`;
`temporal` (change between frames that the reference doesn't have: flicker,
lag, ghost trails); `flicker` (change where the reference is still);
`moving` (error where the reference changes); `sharpness` (gradient on the
reference's strongest 5% of edges, 1 = as sharp); `bias` (mean brightness
error). SSIM alone rewards blur; read it with sharpness and temporal. The raw
input's bias is a few percent low because 1 spp values clip at the linear
tonemap's white point.

## Anti-ghosting and bias (step 5)

Measured on the suite (960x540, 1 spp, 30 frames after 60 warm-up frames,
256-frame references, RTX 3060). Each change was kept only if it measured
better:

- **Catmull-Rom history resampling** (5 bilinear taps, clamped to the four
  surface taps against ringing) where all four taps are valid. Bilinear
  resampling blurs a little every frame while moving: pan sharpness 0.77 to
  0.89.
- **No history feedback.** SVGF writes its first filtered iteration back into
  the history. Here that darkened the cornell view by 7% (the filter's bias
  adds up every frame) and didn't lower the error; without it -3.8% and lower
  RMSE.
- **Geometry-only first iteration.** Comparing luminance on the noisiest data
  favors the darker samples of skewed path tracing noise; leaving it out of the
  3x3 iteration cut the cornell bias to -1.9% and the RMSE to 0.0227.
- **Lighting change detection** (heuristic): the history is compared with the
  current 3x3 neighborhood mean (demodulated luminance); where the difference
  is larger than the neighborhood's standard deviation + 5%, the history is
  shortened in proportion, from the next frame on. Applying it in the same
  frame weighted samples by their own value and darkened the noisiest scenes
  by 10-30%. Big gains for moving light (lights RMSE 0.055 to 0.042, emissive
  0.040 to 0.022), but it still triggers on 1 spp noise in still scenes: the
  cornell view is 7% too dark with it (1.9% without), the emissive room 17%.
  Step 5b replaces it with measured temporal gradients (A-SVGF).
- Tried and dropped: a firefly clamp against history and neighborhood (never
  triggered at 4 sigma), luminance weights against the local mean instead of
  the center (less bias, more blur), a looser luminance sigma for short
  histories (no effect).

| View | RMSE step 4 | RMSE step 5 | Bias step 5 | Sharpness step 5 | Raw input RMSE |
| --- | --- | --- | --- | --- | --- |
| cornell | 0.0285 | 0.0277 | -7.3% | 0.28 | 0.1226 |
| pan | 0.0101 | 0.0074 | -0.3% | 0.85 | 0.0190 |
| emissive | (scene changed) | 0.0218 | -16.8% | 0.12 | 0.0676 |
| skinned | 0.0106 | 0.0072 | -0.6% | 0.80 | 0.0263 |
| thin | 0.0260 | 0.0187 | -0.9% | 0.82 | 0.0284 |
| mirror | 0.0243 | 0.0199 | -0.7% | 0.54 | 0.0234 |
| lights | 0.0548 | 0.0419 | +1.7% | 0.63 | 0.0894 |
| dark | 0.0059 | 0.0054 | -0.9% | 0.31 | 0.0703 |

Sharpness is low on cornell, emissive and dark because their references
still have visible noise on the strongest gradients (bright, small lights).

## Temporal gradients, A-SVGF (step 5b)

After Schied, Peters, Dachsbacher, "Gradient Estimation for Real-Time
Adaptive Temporal Filtering" (HPG 2018). Its reference code is BSD-3-Clause
(Copyright (c) 2018 Christoph Schied, KIT); the notice is in `COPYRIGHT.txt`
and `THIRD_PARTY_LICENSES.md`. The authors ask for a short note when it ships
in a product.

Per frame:

1. **Forward projection** (`RR Forward Project`, before the trace, one thread
   per 3x3 tile of the last frame): a random pixel of the tile takes its
   surface point (last frame's linear depth and view ray), moves it into this
   frame's view (camera motion) and claims the tile it lands in
   (`imageAtomicCompSwap` on `gradient_claim`). The tile's entry
   (`gradient_sample`, `gradient_target`, bindings 44-45 of the path tracer)
   holds the pixel in the tile, last frame's pixel index and seed key, and the
   projected point.
2. **Replay** (raygen, native variant): every pixel writes its seed key to
   `rr_seed` (binding 43; `rng_seed_key()`, `init_rng_from_key()` give the
   same numbers as before). The claimed pixel of a tile instead uses last
   frame's seed key and shoots its primary ray at the projected point. If it
   hits within 1% of the expected distance, the sample is a valid gradient
   sample (`w` = 1). Its result is also this pixel's normal sample.
3. **Gradient** (`RR Gradient`, per tile): replayed luminance minus last
   frame's luminance of the same pixel (`raw_luminance_*`, written by the
   temporal pass), for diffuse and specular, plus the larger of the two. Then
   three a-trous iterations over the tiles (steps 1, 2, 4), weighted by depth
   similarity, average the change and the brightness separately.
4. **Temporal pass**: lambda = |change| / brightness; the blend weight is
   mix(1 / history length, 1, lambda), and the stored history length follows
   it. Where a still scene's light doesn't change, the replay gives exactly
   the old value (same point, same random numbers): lambda is 0 and nothing
   is lost to noise.

The heuristic neighborhood detection of step 5 stays, but only where the
pixel moves on screen (more than a quarter pixel): under camera motion the
history can be stale in ways a gradient at a fixed point doesn't see
(resampling, reflections that change with the view). Without it the moving
camera views got worse (pan RMSE 0.0074 to 0.0098, mirror 0.0199 to 0.0301).

Not done (A-SVGF does it with a visibility buffer): forward projection of
moving objects. The projection only knows camera motion, so on moving
geometry the replayed ray usually misses the expected distance and gives no
gradient there.

Suite (same setup as step 5):

| View | RMSE step 5 | RMSE step 5b | Bias step 5 | Bias step 5b |
| --- | --- | --- | --- | --- |
| cornell | 0.0277 | 0.0228 | -7.3% | -2.1% |
| pan | 0.0074 | 0.0076 | -0.3% | -0.4% |
| emissive | 0.0218 | 0.0127 | -16.8% | -2.9% |
| skinned | 0.0072 | 0.0066 | -0.6% | -0.6% |
| thin | 0.0187 | 0.0202 | -0.9% | -0.9% |
| mirror | 0.0199 | 0.0201 | -0.7% | -0.7% |
| lights | 0.0419 | 0.0256 | +1.7% | +0.4% |
| dark | 0.0054 | 0.0054 | -0.9% | -0.8% |

Moving light: error in moving regions on the lights view 0.046 to 0.028.
The thin view is slightly worse: one pixel in nine repeats an old sample
(the replay), which costs a little where every frame's new information
counts.

Cost at 1920x1080 (RTX 3060, lights view): forward projection 0.04 ms,
gradients and their filter 0.17 ms. Denoiser total 3.58 ms (temporal 0.52,
variance 0.21, five a-trous iterations 2.64). The native path tracer variant
costs 3.50 ms against 2.86 ms for the plain one here (step 10).

## Findings log

Newest first. Note the date, the commit, what you saw or changed.

- 2026-10-10: Step 5b (A-SVGF) done. The still-scene darkening is mostly
  gone (cornell -7.3% to -2.1%, emissive -16.8% to -2.9%) and moving light
  is much better (lights RMSE 0.042 to 0.026). Specular gradients under
  camera motion are partly noise (the replayed specular lobe sample uses the
  new view direction), but measured, dropping them made the mirror view
  worse; kept. Timestamp note: `RR Forward Project` must come before the
  `Pathtracer` timestamp, or the trace time is counted as the projection's.

- 2026-10-10: Steps 5 and 6 done (6 first, to measure 5). The emissive room
  went black for 8 frames in the first suite run: the ball's orbit passed
  through the metal sphere (the room's only light hidden); fixed in the scene.
  Lesson: any history weight that depends on the current samples (detection
  in the same frame, feedback of filtered results) biases skewed path tracing
  noise dark; check `bias` in the suite after every change. Next: A-SVGF
  (step 5b); its reference code is BSD-3-Clause (Schied, KIT), compatible with
  the engine's MIT license if the notice is kept.

- 2026-10-10: Step 4 (specular) done. PBR view, still camera: the mirror
  spheres are clean and sharp (were sparkling). Orbiting camera
  (`--orbit=0.3`, new in the path tracer test project): reflections follow,
  but the sun shadows on the floor are softer than in the 16 spp reference,
  most likely from resampling the history bilinearly every frame (step 5).
  A firefly smeared into a short bright streak on one sphere (step 5). Glass
  view: the alpha blended sphere was a speckled mix of sphere and wall
  (stochastic alpha at the primary hit), the refractive sphere had dark dots
  (guide albedo flipping); both clean now, the refracted pillars a little
  softer than in the reference. The objects with custom shaders were only
  missing because their hit groups compile for the native shader variant on
  first use; at 400 frames they are there.

- 2026-10-10: Step 3 (core SVGF) done. Room view at 1 spp: a still camera
  converges to a clean image within about 30 frames; a moving camera keeps
  surfaces clean, with short history only where something was uncovered
  (screen edges, wall corners sweeping across, the moving ball). PBR view:
  rough spheres and the floor are cleaner than the 16 spp reference without
  denoising; mirror-like and glass spheres still sparkle (history 2, no
  spatial filter: step 4). Objects with custom shaders were missing in one
  denoised PBR shot after 200 frames, probably still compiling their hit
  groups for the new shader variant (check in step 4). Tried skipping
  a-trous neighbors where the noise is below 2% of the signal: no measurable
  gain, the passes are bandwidth bound. Known limits so far: moving objects
  that change depth fail the depth test (the expected depth only accounts
  for camera motion) and restart their history; orthographic cameras aren't
  handled by the position reconstruction.

- 2026-10-09: Step 2 (signal split) done. Verified on the room view (1 spp,
  1280x720, linear tonemap, animation off): the pass-through compose
  (`--denoiser=2`) against no denoiser differs by at most 1/255 (mean
  0.00004), the clean part shows only the emissive panels, the specular part
  the highlights and the shadow shapes of direct light. Smoke tests: PhysX
  GPU (game and editor) print `PhysX 5.10.0 initialized [GPU]`. Two
  problems that are **not** from this change: the glass view exits with
  `ERROR: Attempted to free invalid ID` with denoiser 0 as well, and one of
  two editor smoke runs exited with 0xC0000005 at shutdown (the second run
  was clean).

- 2026-10-09: Step 1 (audit) done, see above. Harness: the DDGI test
  project's `--bench` now also reports the path tracer pass (`pt`) and
  native denoiser passes (`rr_*`, timestamps starting with `RR `).
