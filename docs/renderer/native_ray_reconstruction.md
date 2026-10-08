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
| 2 | Path tracer signal split: demodulated diffuse and specular radiance, clean primary emission/sky/fog, guide buffers for the native denoiser; pass-through compose must match today's image | Open |
| 3 | Core SVGF: `PT_DENOISER_NATIVE`, history resources, reprojection with depth/normal tests, moments, variance, a-trous, compose, timestamps | Open |
| 4 | Specular reconstruction: roughness-aware history and kernel, hit distance, virtual-hit reprojection for glossy surfaces | Open |
| 5 | Anti-ghosting: history clipping, confidence, firefly clamp, NaN guards, disocclusion fallback | Open |
| 6 | Test harness: accumulated reference, metrics (error vs reference, temporal flicker, ghost trails, edge sharpness), moving scenes, thin geometry, mirrors, moving emitters | Open |
| 7 | Settings and debug views: `rendering/ray_reconstruction/*`, presets measured against each other, debug views, editor exposure | Open |
| 8 | History invalidation and upscaler integration: camera cuts (also FSR2/DLSS reset), resize, mode switches, viewport create/destroy; FSR2/TAA/DLSS SR combinations; zero-jitter debug | Open |
| 9 | DDGI review: probe validity and generation, transitions, whether a screen-space pass helps (only if measured) | Open |
| 10 | Performance: per-pass timing, half-resolution diffuse, adaptive a-trous iterations, FP16 storage, pass fusion; 1080p/1440p/4K, native and upscaled | Open |
| 11 | Optional, later: ray traced shadows/reflections for raster mode with their own denoisers | Not planned yet |
| 12 | Documentation, cleanup, merge into `dev` | Open |

## Findings log

Newest first. Note the date, the commit, what you saw or changed.

- 2026-10-09: Step 1 (audit) done, see above. Harness: the DDGI test
  project's `--bench` now also reports the path tracer pass (`pt`) and
  native denoiser passes (`rr_*`, timestamps starting with `RR `).
