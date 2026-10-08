# DDGI: ray traced dynamic diffuse global illumination

Working notes and documentation for the DDGI global illumination option of
this build. This file is the shared memory for humans and agents working on
it: read it before starting, and update the step table and the findings log
as you go. Work happens on the `dev-ddgi` branch.

## What it is

DDGI ("dynamic diffuse global illumination") places grids of irradiance
probes around the camera. Every frame, a budget of probes traces rays with
hardware ray tracing; each probe stores, in two texture atlases, the light
arriving from every direction (an octahedral irradiance map) and the distance
to the nearest surface in every direction (for a visibility test that stops
light leaking through walls). Opaque surfaces then read the probes around
them for their diffuse indirect light. Lights, emissive materials, the sky
and geometry can all change at runtime; the probes follow over a few frames.

It is the Forward+ (rasterized) counterpart of the path tracer: the path
tracer computes all light per pixel, DDGI adds ray traced indirect diffuse
light to the normal rasterizer at a fraction of the cost.

## Licensing and redistribution

The request was to integrate NVIDIA's RTXGI DDGI SDK 1.x. That SDK can't be
used in this fork:

- The SDK is under the **NVIDIA RTX SDKs License**, which says "You may not use
  the SDK in any manner that would cause it to become subject to an open
  source software license", allows source redistribution only for sample code,
  and forbids distributing the SDK as a stand-alone product.
- The shader and source headers say "Any use, reproduction, disclosure or
  distribution ... without an express license agreement from NVIDIA
  CORPORATION is strictly prohibited."
- This fork is public and MIT licensed. Copying or porting SDK source into it
  would put SDK code under the MIT license; that is exactly what the license
  forbids. Linking a prebuilt SDK library is not possible either: the SDK is
  Direct3D 12 / Vulkan host code that manages its own resources and shaders,
  and it would still have to be redistributed with the editor.

So this is an **independent implementation** of the published technique,
written from the papers, not from the SDK:

- Z. Majercik, J.-P. Guertin, D. Nowrouzezahrai, M. McGuire. "Dynamic Diffuse
  Global Illumination with Ray-Traced Irradiance Fields." JCGT 8(2), 2019.
- Z. Majercik, A. Marrs, J. Spjut, M. McGuire. "Scaling Probe-Based Real-Time
  Dynamic Global Illumination for Production." JCGT 10(2), 2021 (probe
  relocation, classification, scrolling volumes).

No RTXGI SDK file was downloaded, read or copied while writing it. All code is
MIT licensed with the rest of the engine; nothing extra ships in releases. The
name "RTXGI" is NVIDIA's; the feature is called DDGI in the engine.

RTXGI 2.x (NRC, SHaRC) is a different technique and isn't used.

## How to use it

1. Forward+ renderer, Vulkan driver (the default for new projects in this
   fork), a GPU with ray tracing.
2. A `WorldEnvironment` with an `Environment`, and a `DDGIVolume` node in
   the same scene (Add Node > `DDGIVolume`).
3. Place and size the volume with its box gizmo, or turn on
   `follow_camera` to let the probes follow the camera (any area).
4. Pick the workload in `Project Settings > Rendering > Global Illumination >
   DDGI > Quality` (Low, Medium, High, Ultra or Custom). It can be changed at
   runtime with `ProjectSettings.set_setting()`.

`DDGIVolume` properties:

| Property | Default | Meaning |
| --- | --- | --- |
| `enabled` | on | DDGI on/off |
| `size` | 24 x 12 x 24 m | Box filled with probes, centered on the node; probes per axis = size / spacing + 1 (at most 64) |
| `probe_spacing` | 1.0 m | Spacing of the finest grid |
| `cascades` | 3 | Probe grids; each one has twice the spacing of the previous |
| `follow_camera` | off | Grids follow the camera and scroll; `size` then only sets the probe count |
| `energy`, `normal_bias`, `view_bias`, `hysteresis`, `probe_relocation`, `probe_classification`, `debug_mode` | | As the Environment properties below |

The node writes these into the `Environment` of its world and turns DDGI
off when it leaves the scene. Its rotation and scale are ignored (the grid
is aligned with the world axes; a configuration warning says so). Its
configuration warnings also tell when the renderer, driver or GPU can't
run DDGI. Use one `DDGIVolume` per scene.

Environment properties (`ddgi_*`). The editor hides them from the
Environment inspector (the `DDGIVolume` node sets them); scripts can still
set them directly:

| Property | Default | Meaning |
| --- | --- | --- |
| `ddgi_enabled` | off | DDGI on/off |
| `ddgi_cascades` | 3 | Probe grids around the camera; each one has twice the spacing of the previous |
| `ddgi_probe_spacing` | 1.0 m | Spacing of the finest grid |
| `ddgi_probe_grid` | 24 x 12 x 24 | Probes per axis per cascade |
| `ddgi_energy` | 1.0 | Indirect light multiplier |
| `ddgi_normal_bias`, `ddgi_view_bias` | 0.1, 0.3 | Sampling offsets (fraction of the spacing) against self shadowing and leaks |
| `ddgi_hysteresis` | 0.95 | Temporal smoothing (higher = less noise, slower response) |
| `ddgi_probe_relocation` | on | Move probes out of geometry |
| `ddgi_probe_classification` | on | Skip probes inside geometry; update probes with nothing nearby rarely |
| `ddgi_follow_camera` | on | Scroll the grids with the camera |
| `ddgi_debug_mode` | Disabled | Debug views (below) |

Project settings (`rendering/global_illumination/ddgi/`):

| Setting | Meaning |
| --- | --- |
| `quality` | Preset: Low 64 rays / 1024 probes per frame / 6x6 irradiance / 12x12 distance; Medium 128 / 2048 / 6 / 14; High 192 / 4096 / 8 / 14; Ultra 256 / 8192 / 8 / 16; Custom |
| `custom_*` | The four workload values for Custom |
| `gpu_time_budget_ms` | Optional: lower the probes traced per frame to stay within this GPU time |

### How it interacts with other GI

- **SDFGI and VoxelGI** are skipped for views that use DDGI (a warning says
  so), so indirect light is never counted twice.
- **LightmapGI**: objects with a lightmap keep it; DDGI lights the rest.
- **Environment ambient light**: DDGI replaces it where probes cover the
  surface (probe rays that miss see the sky, or the ambient color when the
  ambient source is a color). Outside all cascades the normal ambient light
  is used, blended over the outermost probe.
- **Reflections** (sky, reflection probes, SSR) are unchanged: DDGI is diffuse
  only.
- **SSAO / SSIL** still apply on top.
- **Path tracing**: when `pathtracing_enabled` is on, the path tracer computes
  all light and DDGI is not used. Select one or the other per environment.

### Unsupported hardware

DDGI needs `RenderingDevice.SUPPORTS_RAYTRACING_PIPELINE` (Vulkan ray tracing
pipelines). Without it (D3D12 driver, Compatibility or Mobile renderer, GPUs
without ray tracing), DDGI does nothing and a one-time warning explains why;
the scene renders exactly as with DDGI off, including SDFGI/VoxelGI.

### Debugging lighting problems

`Environment.ddgi_debug_mode`:

| Mode | Shows |
| --- | --- |
| Indirect Light | Only the DDGI light (the GI buffer) |
| Probe Irradiance | Probes as spheres with their stored light |
| Probe Distance | Probes shaded by distance to the nearest surface |
| Probe States | Blue = not updated yet, green = active, gray = inactive, red = inside geometry, orange = scrolled in, not traced yet |
| Probe Update Priority | Green = stable, red = changing (updated more often), white = updated this frame |
| Cascades | Surfaces colored by the cascade that covers them |

GPU timings: run with `--gpu-profile`, or open the Visual Profiler in the
editor; the DDGI passes are `DDGI Build Acceleration Structures`, `DDGI
Schedule`, `DDGI Trace Probe Rays`, `DDGI Blend Probes`, `DDGI Relocate
Classify` and `DDGI Apply`.

Common problems:
- Light leaks through a wall: the wall is thinner than about a quarter of the
  probe spacing, or open (single-sided planes). Use closed meshes or reduce
  `ddgi_probe_spacing`.
- Dark blotches near corners: raise `ddgi_normal_bias` / `ddgi_view_bias`.
- Noisy or flickering indirect light: raise the quality (more rays) or
  `ddgi_hysteresis`.
- Slow reaction to light changes: raise the probes per frame (quality) or
  lower `ddgi_hysteresis`.

### Performance tuning

- The ray tracing cost is `rays_per_probe * probes_per_frame`; both come
  from the quality preset. Probes with no surface nearby (classification)
  only trace the fixed rays (a quarter of the rays, at most 32), and get an
  eighth of the update rate.
- Probes in view and probes whose light changes get more of the per-frame
  budget; stable probes behind the camera get less. A feedback factor on
  the GPU raises all rates until the per-frame budget is used.
- Coarser cascades update half as often per level.
- `gpu_time_budget_ms` adapts the probe budget to measured GPU time. It
  never goes below an eighth of the quality's probes per frame, so a budget
  below that floor's cost isn't reached (Ultra in the stress view: about
  0.45 ms on an RTX 3060). Above the floor it settles within about 10% of
  the target; timing noise only pushes it upward near the floor.
- The apply pass runs once per pixel at internal resolution, so it scales
  with the 3D resolution scale (and DLSS / FSR render scale). With
  `rendering/global_illumination/gi/use_half_resolution` (the setting SDFGI
  and VoxelGI use) it runs at half resolution: about a quarter of the cost,
  softer indirect light at object edges.

## Architecture

| Part | Where |
| --- | --- |
| Environment settings and RenderingServer API | `scene/resources/environment.*`, `servers/rendering/storage/environment_storage.*`, `RenderingServer.environment_set_ddgi()` |
| Ray tracing culling also for DDGI | `servers/rendering/renderer_scene_cull.cpp` (`cull.rt_enabled`) |
| DDGI manager (resources, volumes, scheduling, dispatches) | `servers/rendering/renderer_rd/forward_clustered/render_ddgi.*` |
| Hook into the Forward+ frame | `render_forward_clustered.*` (`_ddgi_begin_frame`, `_ddgi_process`, `_ddgi_debug_draw`), implemented in `render_forward_clustered_pt.*` |
| Probe ray tracing | `shaders/raytracing/scene_raytracing_raygen.glsl` (raygen branch `ddgi_trace_probe_rays()`, miss), `raytracing_closest_hit_common_inc.glsl` (`ddgi_probe_ray_shade()`) |
| Data layout, probe addressing, sampling | `shaders/raytracing/ddgi_inc.glsl`, `ddgi_sample_inc.glsl`, `raytracing_ddgi_inc.glsl` |
| Schedule, blend, relocate, classify | `shaders/raytracing/ddgi_update.glsl` |
| Apply (GI buffer) and probe debug view | `shaders/raytracing/ddgi_apply.glsl` |

Per frame, for each view that uses DDGI (inside `_pre_opaque_render()`, after
the depth prepass, where SDFGI/VoxelGI would run):

1. **Acceleration structures**: the path tracer's `RenderRaytracing::build_tlas()`
   builds or refits BLASes (cached per mesh surface; skinned, displaced and
   MultiMesh geometry included) and rebuilds the TLAS. The same TLAS,
   materials and custom-shader hit groups as the path tracer; no second copy.
2. **Schedule** (compute, one thread per probe): resets probes uncovered by
   scrolling, then each probe earns update credit (more when in view or
   changing, less for inactive probes and coarse cascades); probes with a
   full credit go into the update list (up to the per-frame budget).
3. **Trace** (ray tracing pipeline, one ray per thread): the path tracer
   pipeline's raygen, in DDGI mode, traces `rays_per_probe` rays per listed
   probe. The closest hit returns emission + direct light (NEE with shadow
   rays) + albedo times the previous frame's DDGI irradiance (multiple
   bounces over time); back faces return a negative distance. Misses return
   the sky (or ambient color).
4. **Blend** (compute, one workgroup per probe): irradiance (cosine weighted)
   and distance moments (sharp lobe) of the new rays are blended into the
   probe tiles with hysteresis, lowered automatically when the light changed
   a lot; tile borders are refreshed for bilinear filtering.
5. **Relocate and classify** (compute, one thread per probe), from the fixed
   rays: probes inside geometry step through the closest back face, probes
   too close to a surface step away; probes inside geometry are marked
   inside (never sampled), probes with no surface within one cell inactive.
6. **Apply** (compute, one thread per pixel): reconstructs position and
   normal from the depth and normal-roughness buffers, samples the 8
   surrounding probes of the finest covering cascade (trilinear, backface
   and Chebyshev visibility weights), blends cascades over their edges and
   writes the GI ambient buffer. The scene shader mixes it in like SDFGI's
   (`INSTANCE_FLAGS_USE_GI_BUFFERS`).

Scrolling: each cascade's probes are addressed through a toroidal offset, so
when the camera moves by whole probes only the newly covered slab is reset;
a jump larger than the grid resets the cascade. Each cascade snaps to its
own spacing, so coarse cascades scroll less often. Probes in the new slab
get the state "scrolled": the scheduler gives them update slots first, and
until they are traced the sampler uses the atlas texels left by the probes
that scrolled out (stale, but no black holes). Their first update replaces
those texels instead of blending with them.

## Known limitations

- Vulkan with ray tracing only (as the path tracer). The D3D12 driver has no
  ray tracing implementation in this fork.
- Transparent (alpha blended) surfaces don't receive DDGI; they use the
  environment ambient light. Probe rays do pass through alpha-blended and
  refractive materials.
- Diffuse only. Glossy reflections come from the usual sources.
- Lightmapped objects keep their lightmap.
- One view per frame tracks its own probes: split screen or several cameras
  multiply the cost.
- Probe positions are 32-bit floats (as all rendering): very large worlds
  should use origin shifting.
- Thin single-sided geometry (planes) is seen as "inside" from behind;
  classification can then switch off probes behind it. Use closed meshes for
  walls.
- There is no baked/static fallback for hardware without ray tracing.

## Steps

| # | Step | Status |
| --- | --- | --- |
| 1 | Audit: renderer, existing ray tracing, GI paths, RTXGI license | Done |
| 2 | Ray tracing infrastructure: DDGI bindings in the RT scene set, probe ray mode in the RT shaders | Done |
| 3 | Basic DDGI: one fixed volume, trace, blend, sample | Done |
| 4 | Forward+ integration: GI buffer, SDFGI/VoxelGI exclusion | Done |
| 5 | Dynamic scenes: moving lights and objects, BLAS refit | Done |
| 6 | Performance: scheduling, classification, relocation, GPU budget, benchmarks | Done |
| 7 | Scrolling cascades | Done |
| 8 | Editor: settings, debug views, documentation | Done |
| 9 | Compatibility: DLSS, path tracer, D3D12 fallback | Open |
| 10 | Validation: test scenes, benchmarks, this document | Open |

## Test project

[`misc/ddgi_test_project/`](misc/ddgi_test_project/) builds three scenes from
code (`main.gd`):

| View | Contents |
| --- | --- |
| `room` | Closed room: white walls, red and green side walls, orange and blue emissive panels, moving and color-changing omni light, moving box and ball |
| `outdoor` | Sun with a time-of-day cycle, 400 m street of open-fronted buildings and pillars; `--move` flies the camera down the street (scrolling) |
| `stress` | 4000 MultiMesh instances, 40 moving slabs, 32 fast-changing omni lights; `--move` orbits the camera |

```
bin\godot.windows.editor.x86_64.exe --path misc\ddgi_test_project
bin\godot.windows.editor.x86_64.console.exe --path misc\ddgi_test_project -- --view=room --gi=ddgi --shot=C:\tmp\room.png
bin\godot.windows.editor.x86_64.console.exe --gpu-profile --path misc\ddgi_test_project -- --view=stress --quality=2 --res=1920x1080 --frames=300 --bench=300
```

Arguments (after `--`): `--view=`, `--gi=none|sdfgi|ddgi`, `--quality=0..4`,
`--cascades=`, `--spacing=`, `--grid=x,y,z`, `--hysteresis=`, `--energy=`,
`--debug=0..6`, `--anim=0|1`, `--tod=0|1` (sun cycle), `--move=1`,
`--speed=`, `--pt=1`, `--scale3d=6 --scale=0.67` (DLSS), `--res=WxH`,
`--frames=N` (warm-up), `--shot=path`, `--bench=N`, `--instances=`, `--lights=`,
`--linear --exposure=`, `--budget=ms` (GPU time budget), `--half=1` (half
resolution apply), `--settle=N` (with `--shot`: stop the camera, wait N
frames and save `<shot>_settled.png`; the difference to the first shot is
the error that moving leaves).

Keys: `1`-`3` views, `G` GI mode, `U` quality, `Tab` debug mode, `L`
animation, `M` camera path, `P` path tracing; fly camera with the right mouse
button and WASD/QE.

## Benchmarks

Measured with the benchmark harness above. GPU: see each run. Times are
averages over the measured frames, after warm-up.

RTX 3060, driver 616.92, Vulkan, 1920x1080 native, 300 warm-up + 300
measured frames, `--gpu-profile`, commit `835a780893`. Default volumes (3
cascades of 24 x 12 x 24, 1 m spacing). "Frame" is the whole frame's GPU
time; "DDGI" is the sum of the DDGI passes. All times in ms.

| View | GI | Frame | DDGI | AS | Trace | Blend | Apply |
| --- | --- | --- | --- | --- | --- | --- | --- |
| room | none | 1.28 | | | | | |
| room | Low | 2.30 | 0.95 | 0.11 | 0.12 | 0.08 | 0.63 |
| room | Medium | 2.61 | 1.26 | 0.11 | 0.30 | 0.20 | 0.63 |
| room | High | 3.06 | 1.72 | 0.10 | 0.57 | 0.42 | 0.61 |
| room | Ultra | 4.10 | 2.76 | 0.10 | 1.13 | 0.89 | 0.62 |
| outdoor | none | 0.86 | | | | | |
| outdoor | Low | 2.01 | 1.07 | 0.10 | 0.26 | 0.10 | 0.60 |
| outdoor | Medium | 2.74 | 1.79 | 0.10 | 0.81 | 0.27 | 0.59 |
| outdoor | High | 4.50 | 3.53 | 0.10 | 2.06 | 0.75 | 0.59 |
| outdoor | Ultra | 8.21 | 7.26 | 0.10 | 4.70 | 1.75 | 0.68 |
| stress | none | 1.50 | | | | | |
| stress | Low | 2.34 | 0.77 | 0.10 | 0.12 | 0.07 | 0.46 |
| stress | Medium | 2.72 | 1.12 | 0.11 | 0.34 | 0.18 | 0.47 |
| stress | High | 3.25 | 1.70 | 0.10 | 0.71 | 0.41 | 0.46 |
| stress | Ultra | 4.49 | 2.96 | 0.10 | 1.48 | 0.89 | 0.46 |

Variants (Medium unless noted):

| Run | Frame | DDGI | Trace | Blend | Apply |
| --- | --- | --- | --- | --- | --- |
| outdoor, camera moving (scrolling) | 2.75 | 1.84 | 0.86 | 0.28 | 0.58 |
| stress, camera orbiting | 2.69 | 1.15 | 0.38 | 0.22 | 0.44 |
| room, half resolution apply | 2.10 | 0.77 | 0.28 | 0.20 | 0.17 |
| stress, half resolution apply | 2.34 | 0.77 | 0.33 | 0.19 | 0.13 |
| outdoor, 1 cascade | 2.02 | 1.08 | 0.47 | 0.27 | 0.23 |
| stress Ultra, budget 0.25 ms | 2.59 | 1.03 | 0.28 | 0.15 | 0.48 |
| stress Ultra, budget 0.5 ms | 2.73 | 1.17 | 0.38 | 0.20 | 0.47 |
| stress Ultra, budget 1.0 ms | 3.21 | 1.67 | 0.70 | 0.38 | 0.47 |
| stress Ultra, budget 2.0 ms | 4.12 | 2.57 | 1.24 | 0.74 | 0.47 |

Schedule (0.01-0.02 ms) and relocate/classify (0.01 ms) are left out of
the tables. Without `--gpu-profile`, the stress Ultra frame is 4.60 ms
unbudgeted and 2.70 ms with a 0.5 ms budget, so the budget also works
when the profiler is off.

How to read them:
- Apply is a fixed cost per pixel (about 0.6 ms at 1080p with every pixel
  covered; less where the sky shows). Half resolution apply cuts it to
  about a quarter. That is the cheapest saving at Low and Medium.
- Trace and blend scale with rays x probes, but only for active probes.
  Inside probes skip the blend, and inactive probes trace only the 32
  fixed rays. The open street has far more active probes than the closed
  room, so its trace and blend cost twice as much at the same quality.
- Scrolling (moving camera) costs about 3% more than a still camera.
- Acceleration structure upkeep (refit and TLAS) is about 0.1 ms in every
  view; it is shared with the path tracer.

## Findings log

Newest first. Note the date, the commit, what you saw or changed.

- 2026-10-09: Step 8 (editor) done. Reviewed the `DDGIVolume` node and
  fixed: removing the node left DDGI on in the Environment (fixed in place
  at the first camera position), now it turns DDGI off; camera-following
  DDGI couldn't be enabled in the editor (the Environment properties are
  hidden), now `DDGIVolume.follow_camera`; the defaults were tuned to one
  scene (60.264 x 5.774 x 62.86 m, 2 m spacing, relocation and
  classification off), now 24 x 12 x 24 m, 1 m, both on (scenes that
  relied on the old defaults must set them); the base class notification
  ran twice. Added configuration warnings for D3D12, GPUs without ray
  tracing pipelines and a rotated node, the class reference
  (`doc/classes/DDGIVolume.xml`, plus `environment_set_ddgi_volume` in
  `RenderingServer.xml`), an editor icon and the full license header.
  Checked with a headless script: defaults, enable/disable, grid from
  size, follow camera, removal, and a scene where the volume enters before
  the WorldEnvironment; all pass, no leaks.

- 2026-10-09: Step 7 (scrolling cascades) done. Scrolling itself was in
  place (toroidal addressing per cascade, slab reset, two-pass scheduler,
  stale fallback). Found and fixed one problem: a scrolled-in probe was
  set to active and its first trace blended with the hysteresis (0.95 for
  distance, at least 0.38 for irradiance) against the texels of the probe
  that had scrolled out at the far side of the grid. The wrong distance
  moments broke the visibility test for many updates (light leaking at the
  edges of building interiors while moving). Scrolled probes now have
  their own state (`DDGI_PROBE_SCROLLED`, orange in the probe states
  view); they are still sampled until traced, and the first update
  replaces their data like a new probe's.
  Measured with `--settle` (outdoor, Medium, indirect light view, 1280x720,
  moving shot against the shot 600 frames after stopping, 3 runs each):
  at 8 m/s the mean error went from 4.05 to 3.44 and the pixels off by
  more than 16/255 from 4.6% to 3.5%; at 30 m/s no difference beyond the
  noise (3.5 to 3.6, 2.2% to 2.0%). The noise floor with a still camera
  is 1.5 mean and 0.04%. Startup convergence is unchanged.
  The rest of the moving error is multi-bounce lag: new probes see
  neighbors that have no bounced light yet, and that settles over
  seconds (still 2.3% of pixels off 120 frames after stopping). Tried and
  dropped, because they didn't help: tracing relocated probes again in
  the next frame (error up by 0.2-0.3), and averaging the first updates
  evenly instead of with the hysteresis (better after 30 frames, worse
  after 10, because the light that is still building up gets averaged in).

- 2026-10-08: Step 6 done. Rechecked the code: scheduling (two passes,
  credit, in-view and variability weights, `rate_scale` feedback),
  classification, relocation, the GPU time budget and the blend/apply
  optimizations are all in place. Filled the benchmark tables (RTX 3060,
  commit `835a780893`) and added `--budget=` and `--half=1` to the harness.
  Budget check: targets 0.5 / 1.0 / 2.0 ms gave 0.60 / 1.10 / 2.00 ms of
  update work; 0.25 ms stops at the floor (an eighth of the probes, 0.45
  ms). Outdoor experiment: with 1 cascade instead of 3, trace drops from
  0.81 to 0.47 ms and apply from 0.59 to 0.23 ms. Fewer probes are active,
  and fewer probe hits and pixels fall inside a volume. Possible later
  savings, not done because they change the image: no visibility test in
  the multi-bounce lookup of probe hits, and a blend that bins rays per
  texel instead of looping over all of them.

- 2026-10-08: Added `DDGIVolume` as the editor-facing DDGI control. The node
  owns enable, size, probe spacing/grid, cascade count, energy, bias,
  hysteresis, relocation/classification, and debug mode. Its transform places
  the volume center and its box gizmo resizes width/height/depth in the 3D
  viewport. Environment DDGI properties stay available for compatibility but
  are hidden from the Environment inspector; the node writes a fixed DDGI volume
  into the active WorldEnvironment and disables camera-following for it.

- 2026-10-08: Fixed black flicker while moving through scrolling DDGI volumes.
  The scheduler used one unordered atomic list, so stable/old probes could take
  the per-frame slots before newly uncovered scrolled probes. Those probes were
  reset to `DDGI_PROBE_NEW`, so surfaces sampling them could fall back to black.
  Scheduling now runs in two passes (new/full-reset/scrolled probes first, all
  other due probes second), and scrolled probes keep their previous atlas texels
  as a stale-lighting fallback until retraced. Full resets still start new.

- 2026-10-08: Follow-up for black DDGI spots on moving camera / probe debug.
  Inactive probes (no nearby surface) can still have black or stale irradiance
  tiles, especially around single-sided level meshes and sparse geometry. The
  surface sampler now ignores inactive probes the same way it already ignores
  new and inside probes, so those tiles can't darken nearby meshes while active
  neighbors provide the lighting. If no active neighbor covers the point, DDGI
  returns zero coverage and the renderer keeps normal environment ambient.

- 2026-10-08: Step 6 (performance), first pass. Measured with the bench
  harness on an RTX 3060 (the validation machine's GPU; the RTX 5070 named
  in the request wasn't available), room scene, 1920x1080, Medium. Blend
  passes re-read every ray per texel: rays now go to shared memory once
  per workgroup (0.52 -> 0.21 ms). The apply pass did dozens of integer
  divisions per pixel (probe index wrap, atlas tile address): the scroll
  wrap is a conditional subtraction now and the atlas row length a power of
  two (1.04 -> 0.62 ms). DDGI total 2.0 -> 1.27 ms per frame. Timestamps
  now end with `DDGI Done`, so the apply time no longer includes the light
  cluster setup that follows it.

- 2026-10-08: Step 5 (dynamic scenes). Light switch test (`--switch`: all
  lights and emitters off, a new emissive ceiling panel on) showed the probes
  froze after their first update. Instrumented one probe: it was scheduled
  every frame and its rays were fresh, but irradiance and variability never
  changed. Cause: the blend passes used an indirect dispatch whose group
  count came from the update list header written by the scheduler in the
  same frame; the dispatch didn't see it. Now a direct dispatch over the
  per-frame capacity (groups past the list count exit at once). Two more
  fixes from the same measurements: (1) the "lighting changed" detection
  per texel fired on ray noise, so hysteresis dropped all the time; it now
  uses the change of the probe's tile average, and lowers hysteresis
  smoothly (to at most 40% of the setting). (2) The scheduler used about
  10% of the budget (inactive and stable probes have low rates); a
  persistent `rate_scale` (DDGIStats buffer) now rises while the budget
  isn't filled and falls when it overflows. Result: after the switch the
  room's GI settles in about 20 frames (linear indirect, ±3% noise after).

- 2026-10-08: Steps 3-4 working (RTX 3060, Vulkan). Room view: red and
  green bleed onto the boxes and floor, the orange and blue panels light the
  ceiling, and the closed room no longer gets blue sky ambient in its
  shadows (compare `--gi=none`). Outdoor: shadowed walls pick up bounce
  light. Bug found: isolated black pixels on curved surfaces. Cause: Forward+
  stores "best fit" scaled normals in the normal-roughness buffer, so their
  encoded length isn't 1; the apply pass rejected short ones. Only depth
  decides sky vs geometry now. Also added NaN guards in the blend and apply
  passes (a NaN would stay in a probe forever through the hysteresis).

- 2026-10-08: Steps 1-2. Audit: the fork already has Vulkan BLAS/TLAS
  management with refit, MultiMesh merging, skinned and displaced geometry,
  StandardMaterial and custom shader hit groups, light gathering with NEE,
  and a wide RT culling volume, all built for the path tracer. DDGI reuses
  all of it. The RT pipeline layout is defined by one shader, so instead of
  a second raygen shader the raygen branches on `RT_PARAM_DDGI_TRACE`. DDGI
  resources take set 0 bindings 34-39 (stand-ins when DDGI is off). Probe
  rays carry `DDGI_PROBE_RAY_FLAG` and count as an indirect bounce, so the
  camera-only writes (depth, motion vectors, DLSS guides, volumetric fog)
  are skipped. RTXGI SDK license reviewed: incompatible with this MIT fork,
  so DDGI is implemented from the papers.
