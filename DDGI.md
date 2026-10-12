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
3. Place and size the volume with its box gizmo (or set `probe_count`), or
   turn on `infinite_world` to let the probes follow the camera (any area).
4. Pick the workload in `Project Settings > Rendering > Global Illumination >
   DDGI > Quality` (Low, Medium, High, Ultra or Custom). It can be changed at
   runtime with `ProjectSettings.set_setting()`.
5. Optional: bake the probes (below), so the game starts with finished
   indirect light, or doesn't trace at all.

`DDGIVolume` properties:

| Property | Default | Meaning |
| --- | --- | --- |
| `enabled` | on | DDGI on/off |
| `size` | 24 x 12 x 24 m | Box filled with probes, centered on the node; probes per axis = size / spacing + 1 (at most 64) |
| `probe_count` | 25 x 13 x 25 | Probes per axis (width, height, depth) of each cascade, 2 to 64; linked to `size` (size = (count - 1) x spacing), not saved |
| `probe_spacing` | 1.0 m | Spacing of the finest grid |
| `cascades` | 3 | Probe grids; each one has twice the spacing of the previous |
| `infinite_world` | off | Unlimited world: the grids are centered on the camera and scroll with it; `probe_count` (or `size`) then only sets the probes per cascade. Can't be baked. (Called `follow_camera` in earlier dev builds; old scenes still load.) |
| `bounce_energy` | 1.0 | Multiplier for the light passed on from bounce to bounce inside the probes; above 1, indirectly lit rooms get brighter (0 to 2) |
| `realtime_updates` | off | Every probe every frame (fewer rays per update): light changes such as weather follow evenly and faster; more GPU time |
| `bake_mode` | Dynamic | Dynamic: traced at runtime. Baked: only the baked probes, no rays (also without ray tracing hardware). Baked + Dynamic: start from the bake, then update |
| `probe_data` | | The baked probes (`DDGIProbeData`) |
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
| `ddgi_bounce_energy` | 1.0 | Multiplier for the light passed on from bounce to bounce (0 to 2) |
| `ddgi_realtime_updates` | off | Every probe every frame |
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
| `light_transition_time` | 0.2 s (at 60 fps): how long surfaces take to fade to a probe's new light |
| `realtime_rays_per_probe` | 64: rays per probe and frame with realtime updates |
| `emissive_lights` | On: emissive surfaces light pixels directly (sphere lights with a ray traced shadow); the probes carry only their bounce light |
| `near_field_rays` | On: one short ray per block of the low resolution apply and frame, accumulated and filtered, in place of the probes' light near surfaces (see below) |
| `near_field_distance` | 0.5: how far the near-field rays reach, in probe spacings of the finest volume (moving objects reach 2 regardless) |
| `skip_moving_objects` | On: probes and near-field cosine rays skip objects that keep moving; each block takes the light of the one that covers most of its view, from its shape (a sphere, or its oriented box scaled by how much of it the mesh fills), so its shadow and bounce light follow it without noise or smear |

### Baking

Select the `DDGIVolume` and press **Bake DDGI** in the 3D editor toolbar
(or call `DDGIVolume.bake()` from a script). The bake runs on the GPU (the
same hardware ray tracing as at runtime). It renders a tiny offscreen view
and traces up to 16384 probes per frame, not the realtime budget, until
every probe has been updated about 96 times: a third with hysteresis 0.85
so the bounced light builds up, then a running average (weight 1/n). The
editor's 3D views pause meanwhile. Then it reads the atlases back and
saves them as a `DDGIProbeData` resource (`<scene>.<node>.ddgi.res`,
compressed). The interior test scene (4968 probes, Medium) bakes in about
0.5 s into 3.6 MB; 20736 probes (3 cascades of 25 x 13 x 25) in 1.2 s.

With `bake_least_squares` (default on) the bake then refines the probes
(`scene/3d/ddgi_bake_solver.cpp`, after Call of Duty: Infinite Warfare's
light grid bake). It bakes the volume a second time with half the probe
spacing (2n - 1 probes per side: the coarse probes and the points between
them), then solves, per cascade, octahedral texel and color channel, for
the coarse irradiance whose blend at each fine probe, with the renderer's
own weights (trilinear, backface, Chebyshev visibility from the coarse
distances, crush, in square root space), best matches the fine probe's
light. Two regularizers: smoothness between neighboring probes that see
each other (not through walls) and a pull toward each probe's own traced
light. Conjugate gradients, 48 iterations, all texels in parallel. Only
active fine probes (near surfaces) are used. Volumes with more than 32
probes per side skip it (the fine grid would exceed 64). The interior
bakes in 6.0 s instead of 0.5 s (solve 0.5 s).

| Bake mode | At runtime |
| --- | --- |
| Dynamic | The bake is ignored; probes are traced as before |
| Baked | Only the baked probes are sampled: no ray tracing, no update cost, no noise or flicker. The light doesn't follow changes. Works on the D3D12 driver and on GPUs without ray tracing |
| Baked + Dynamic | The probes start from the bake (no dark, blotchy first seconds) and are then updated as in Dynamic |

The bake belongs to one volume layout: moving or resizing the volume, or
changing its probe spacing or cascades, makes it unusable until baked again
(a configuration warning says so; meanwhile the probes are traced
dynamically when the hardware can). The atlas resolution of the bake (from
the quality setting at bake time) is kept at runtime; rays and probes per
frame still come from the current quality. `bounce_energy` changes what the
probes see, so bake again after changing it. Baking needs ray tracing
(Vulkan, a GPU with ray tracing pipelines) and a fixed volume
(`infinite_world` off).

### Smooth light changes (weather, day and night)

A light change reaches the probes one by one: each is updated in its turn,
and each update is a step. Seen directly, a storm rolling in dimmed the
scene in blotches, probe by probe. Two things deal with it:

- **Light transition** (always on): the probes write one irradiance atlas,
  surfaces read a second one that eases toward the first every frame
  (a 0.1 ms compute pass at 1080p). A probe's step then fades in over
  `light_transition_time` (0.2 s at 60 fps), so the whole scene changes
  evenly. It also removes the last flicker of noisy probes. The first
  update of a new or scrolled-in probe is shown at once.
- **Realtime updates** (`DDGIVolume.realtime_updates`): every probe every
  frame, in chunks of up to 16384 neighboring probes (index order, so a
  slab of the grid at a time), with `realtime_rays_per_probe` rays (64)
  and a hysteresis matched to the per-frame updates (0.95 becomes 0.9).
  Distances are blended every 4th frame only. The light follows about
  twice as fast and over the whole scene at once, including far cascades.
  It can be switched on only while a weather change runs; switching keeps
  the probes' light.

Weather test (outdoor view, Medium, a storm over 2 s: sun 1.5 to 0.25 and
cold, sky 1.0 to 0.25; frames until the indirect light is within 2 levels
of its final value after the storm ends; RTX 3060, 1080p for the costs):

| Mode | DDGI ms | Lag outdoor / interior | Flicker, static interior |
| --- | --- | --- | --- |
| Before (no transition) | 1.63 | 72 / 57 frames, blotchy | 0.4 - 0.9% of pixels |
| Budgeted + transition | 1.72 | 100 / 67 frames, even | 0.02% |
| Realtime (64 rays) + transition | 3.61 | 58 / 36 frames, even | 3.5% |
| Realtime, 128 rays | 6.39 | | 0.12% |

### Moving objects and noise (temporal gradient, guided rays)

Two additions to the probe update, both written from papers (no code taken
from their GPL / unknown-license implementations):

- **Temporal gradient** (after A-SVGF, Schied, Peters, Dachsbacher 2018).
  The fixed rays point the same way every update, so each probe remembers
  their light and hit distance (`DDGIFixedHistory`, two halfs per ray). A
  fixed ray whose hit distance changed saw geometry move; the share of a
  texel's light (or of the whole probe's, if larger) carried by such rays
  lowers that texel's hysteresis. Static scenes never trigger it (distances
  are exact), so it adds no flicker. Moving objects' bounce light and
  shadows follow about twice as fast.
- **Guided rays** (after Real-Time Markov Chain Path Guiding, Alber,
  Hanika, Dachsbacher 2025). Each probe keeps a moving average of where its
  light comes from (`DDGIProbe.guide`). When the light comes mostly from one
  side (a window, a sunlit patch, an emitter), up to half of the rotated
  rays are sampled from a von Mises-Fisher lobe around it; every ray is
  weighted by uniform pdf / mixture pdf, so the result stays unbiased. Light
  from all around (open sky) isn't guided.

Measured (RTX 3060, 1080p, Medium; ghost test: an emitter in front of a
wall is moved away, frames until its light on the wall is half gone /
settled):

| | Before | Gradient | Gradient + guiding |
| --- | --- | --- | --- |
| Ghost, budgeted | 28 / 82 frames | 15 / 54 | 15 / 55 |
| Ghost, realtime | 25 / 61 | 17 / 51 | 16 / 49 |
| Interior flicker std (budgeted) | 0.214 | 0.216 | 0.177 |
| Interior flicker pixels (realtime) | 3.5% | 3.53% | 1.73% |
| DDGI ms outdoor, budgeted / realtime | 1.87 / 3.71 | 1.91 / 3.85 | 1.97 / 4.03 |

Getting the guiding's noise reduction from more rays instead would take
about 1.5 times the rays (+0.36 ms of tracing at Medium); guiding costs
+0.06 ms. It doesn't replace a quality level: Low with guiding is still
noisier than Medium without (interior std 0.255).

### Dark interiors

DDGI is physically based: a room lit through one window is dark. The
`interior` test view, compared with the path tracer, is about as dark (DDGI
is slightly brighter), so the probes don't lose light. To make interiors
brighter without flicker:

- Raise `bounce_energy` (1.5 to 2): more light passed on between surfaces.
  It is averaged inside the probes, so it brightens without flicker (in the
  interior view, 2.0 gives +28% overall and less flicker).
- Use auto exposure (`CameraAttributes`), as for any interior.
- `energy` scales the final indirect light (including the parts that are
  already bright).

### How it interacts with other GI

- **SDFGI and VoxelGI** are skipped for views that use DDGI (a warning says
  so), so indirect light is never counted twice.
- **LightmapGI**: objects with a lightmap keep it; DDGI lights the rest.
- **Environment ambient light**: DDGI replaces it where probes cover the
  surface (probe rays that miss see the sky, or the ambient color when the
  ambient source is a color). Outside all cascades the normal ambient light
  is used, blended over the outermost probe. A point inside a cascade whose
  surrounding probes are all inside geometry (a cave or tunnel narrower
  than the probe spacing) counts as dark, never as the ambient light.
- **Volumetric fog**: where probes cover it, its ambient light
  (`volumetric_fog_ambient_inject`) comes from the probes instead of the
  unoccluded sky, so fog in caves and interiors stays dark.
- **Reflections**: DDGI is diffuse only, but it occludes the sky and
  reflection probe light: the scene shader scales it by the DDGI irradiance
  over the environment's (unoccluded) ambient light at the surface, about 1
  in the open and near 0 in caves and tunnels. SSR is unchanged.
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
Schedule`, `DDGI Trace Probe Rays`, `DDGI Bounce Light`, `DDGI Blend
Probes` (irradiance), `DDGI Blend Distance`, `DDGI Relocate Classify`, `DDGI
Smooth` and `DDGI Apply` (the low resolution probe sampling). While the
profiler runs, the apply is split further: `DDGI NF Trace`, `DDGI NF
Resolve`, `DDGI NF Temporal`, `DDGI NF Filter` (near-field rays),
`DDGI Upsample` and `DDGI Mark`.

Common problems:
- Light leaks through a wall: the wall is thinner than about a quarter of the
  probe spacing, or open (single-sided planes). Use closed meshes or reduce
  `ddgi_probe_spacing`.
- Dark blotches near corners: raise `ddgi_normal_bias` / `ddgi_view_bias`.
- Noisy or flickering indirect light: raise the quality (more rays) or
  `ddgi_hysteresis`, or bake the probes (Baked has no noise at all).
- Slow reaction to light changes: raise the probes per frame (quality) or
  lower `ddgi_hysteresis`.

### Performance tuning

- The ray tracing cost is `rays_per_probe * probes_per_frame`; both come
  from the quality preset. Probes with no surface nearby (classification)
  only trace the fixed rays (a quarter of the rays, at most 32), and get an
  eighth of the update rate.
- Probes that light visible surfaces and probes whose light changes get
  more of the per-frame budget: a mark pass after the apply pass (one pixel
  per 8x8 tile, at a position that rotates every frame) records which
  probes surfaces sample, and probes marked in the last 16 frames get 2x
  the rate. Probes in the view frustum that no surface samples (behind
  walls) get 0.8x, probes off screen 0.6x. A feedback factor on the GPU
  raises all rates until the per-frame budget is used.
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
- `rendering/global_illumination/ddgi/apply_resolution` (default 1/4)
  samples the probes once per block of 2x2, 4x4 or 8x8 pixels instead of
  per pixel: at the block's surface (2x2 pixels of the block; alternate
  blocks take the nearest or the farthest as reference, as in SDFDDGI's
  min/max checkerboard, and average the pixels near its depth), for the
  normal and four directions tilted 27 degrees
  around it, which gives the irradiance and its gradient over the normal.
  An upsample pass then writes the GI buffer: each pixel blends the 2x2
  blocks around it by bilinear weight x depth match x normal match and
  evaluates their light at its own normal, so normal maps keep their
  indirect shading. 1080p, RTX 3060: apply 0.717 ms at full, 0.372 at 1/4,
  0.309 at 1/8. The upsample (about 0.2 ms, reading depth and normals and
  writing the GI buffers at full resolution) is the floor; with the half
  resolution GI buffer the low resolution apply saves nothing below 1/8.

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
| Baking: node, resource, editor button | `scene/3d/ddgi_volume.*` (`DDGIProbeData`, `DDGIVolume::bake()`), `editor/scene/3d/ddgi_volume_editor_plugin.*` |
| Baking: readback, upload, baked only | `RenderDDGI::get_probe_data()`, `_upload_baked()`, `update_baked()`; `RenderingServer.viewport_get_ddgi_probe_data()`, `environment_set_ddgi_baked_data()` |

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
   rays); back faces return a negative distance. Misses return the sky (or
   ambient color). For front faces it also writes the albedo, normal and hit
   point (relative to the probe) into the ray bounce buffer.
   **Bounce light** (compute, one thread per ray) then adds albedo times the
   previous frame's DDGI irradiance at the hit (multiple bounces over time).
   The lookup used to run in the closest hit, where it took about 70% of the
   trace time; in compute it costs a fraction of that.
4. **Blend** (compute, one workgroup per probe): irradiance (cosine weighted)
   and distance moments (sharp lobe) of the new rays are blended into the
   probe tiles with hysteresis. It is lowered when the light changed by
   more than the ray noise in two updates in a row, and raised for texels
   whose light comes from a few bright rays; tile borders are refreshed for
   bilinear filtering.
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
7. **Near-field rays** (`near_field_rays`, with the low resolution
   apply): the low resolution pass also writes one cosine-distributed ray
   per block. The ray tracing pipeline traces them like probe rays (raygen
   mode `RT_PARAM_DDGI_TRACE` 3/4, up to `near_field_distance` spacings;
   the update list and ray bindings carry the rays). A resolve pass adds
   the probes' light at the hits (bounce light) and takes the probes'
   irradiance at the block for rays that hit nothing. The samples are
   accumulated per block (reprojected with the camera, matched by depth and
   normal, clamped to the 5x5 neighborhood, blend at least 0.06), filtered
   (two edge-aware 5x5 passes) and replace the probes' irradiance before the
   upsample. Probes alone put light from a nearby object (a red ball next
   to a white wall) as a soft blob around the closest probes that trails
   the object; with the rays it sits next to the object and follows it.

Scrolling: each cascade's probes are addressed through a toroidal offset, so
when the camera moves by whole probes only the newly covered slab is reset;
a jump larger than the grid resets the cascade. Each cascade snaps to its
own spacing, so coarse cascades scroll less often. Probes in the new slab
get the state "scrolled": the scheduler gives them update slots first, and
until they are traced they aren't sampled (their atlas texels belong to the
probes that scrolled out, at the far side of the grid); the next coarser
cascade covers them. Their first update replaces those texels instead of
blending with them, and their next updates are averaged (weight 1/n, up to
the hysteresis) and come four times as often until the probe has 8, so the
noise of the first updates doesn't linger.

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
- Without ray tracing hardware only baked probes work (Bake Mode: Baked),
  and they don't follow light changes.
- Baked probes belong to one volume position and size.

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
| 11 | Interiors: flicker in dark rooms, bounce energy | Done |
| 12 | Baking: `DDGIProbeData`, bake modes, editor button, baked-only without ray tracing | Done |
| 14 | Probe count per axis, infinite world toggle, faster bake | Done |
| 15 | Smooth light changes: light transition, realtime updates | Done |
| 16 | Dark caves (no sky ambient fallback, DDGI-lit volumetric fog), scrolling blotches | Done |
| 17 | Moving objects: temporal gradient from the fixed rays; guided probe rays | Done |
| 18 | Black surfaces: custom-shader geometry in the TLAS while its hit group compiles; inactive probes sampled | Done |
| 19 | Dark areas on par with the path tracer: DDGI occludes sky reflections | Done |
| 20 | Performance: bounce light lookup moved out of the ray tracing pipeline | Done |
| 22 | Scheduling: probes sampled by visible surfaces first (mark pass) | Done (branch `dev-ddgi-sched`) |
| 23 | Performance: low resolution apply with directional output and upsampling | Done (branch `dev-ddgi-quality`) |
| 24 | Baking: least-squares refinement against a half-spacing bake | Done (branch `dev-ddgi-quality`) |
| 25 | Near-field rays in the apply pass: bounce light from nearby objects in place, following them | Done (branch `dev-ddgi-quality`) |
| 26 | Moving objects out of the probes; near-field history lets go faster near them | Done (branch `dev-ddgi-quality`) |
| 27 | Moving objects lit as spheres by one ray per block: no noise | Done (branch `dev-ddgi-quality`) |
| 28 | Moving objects' light checked against the path tracer: direct light only, hidden light toward the object, smoothed | Done (branch `dev-ddgi-quality`) |
| 29 | Moving objects of any shape: oriented boxes with exact face form factors, light averaged per side | Done (`dev-ddgi`, 0.7.1) |
| 30 | No flicker or blotches in dark places: near-field history holds the correction to the probes, reset by the probes' own change; close hits use the block's probe light | Done (`dev-ddgi`) |

## Test project

[`misc/ddgi_test_project/`](misc/ddgi_test_project/) builds five scenes from
code (`main.gd`):

| View | Contents |
| --- | --- |
| `room` | Closed room: white walls, red and green side walls, orange and blue emissive panels, moving and color-changing omni light, moving box and ball |
| `outdoor` | Sun with a time-of-day cycle, 400 m street of open-fronted buildings and pillars; `--move` flies the camera down the street (scrolling) |
| `stress` | 4000 MultiMesh instances, 40 moving slabs, 32 fast-changing omni lights; `--move` orbits the camera |
| `interior` | Closed house lit only by the sun through one window, and a windowless back room behind a doorway (bounce light only); use `--tod=0` |
| `tunnel` | A closed 12 m tunnel open at one end onto sunlit ground: dark indirect light only. Use `--tonemap_exposure=6`; `--plain` without the grid texture (path tracer), `--overlap` lets the walls reach into the roof |
| `bleed` | A red ball bouncing in front of a 2 m white wall; bright sun from the side and slightly behind the wall lights the ball, the wall front stays out of direct sun (`--sun=<energy>`, default 4). `--shape=box|pole|plank|capsule` for other shapes (they also turn), `--jitter` with `--anim=0` keeps the object in place but moving (path tracer comparisons), `--nf=0` (near-field rays off), `--skip_moving=0` |

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
`--linear --exposure=`, `--budget=ms` (GPU time budget), `--apply_res=0..3`
(apply resolution: full, 1/2, 1/4, 1/8), `--half=1` (half
resolution apply), `--settle=N` (with `--shot`: stop the camera, wait N
frames and save `<shot>_settled.png`; the difference to the first shot is
the error that moving leaves), `--reloc=0|1`, `--classify=0|1`,
`--bounces=` (path tracer), `--bounce=` (bounce energy), `--realtime=1`
(realtime updates), `--rt_rays=` (realtime rays per probe), `--weather=dir`
(a storm over `--weather_frames`, every frame saved; see "Smooth light
changes"). `--chaos=N` (`--seed=`) makes N random runtime changes, 20
frames apart (quality, apply resolution, near-field rays, realtime updates,
half resolution, debug modes, GI mode, view, viewport size, 3D scale,
cascades, spacing, grid) and prints `CHAOS done`: a stability test, pass =
no `ERROR` lines and no crash.

Flicker and brightness: `--measure=N` captures N frames and prints mean
linear brightness, the mean temporal deviation per pixel (8-bit levels) and
the share of pixels deviating by more than 2 levels; `--mean=path` and
`--stdmap=path` save the mean image and a deviation map (white = 8 levels),
`--trace_px=x,y` prints one pixel per frame, `--trace_mean=1` the image
mean of every captured frame (`MEANF frame value`). Use `--anim=0 --tod=0`:
a perfect result then has no flicker at all.

Moving light: `--emissive_step=1` puts a bright emissive sphere in the
interior's lit room and moves it 4.5 m when the measurement starts. With
`--sun_step=0 --debug=1 --frames=400 --measure=600` it prints the frames
until the image is 50%, 90% and 98% of the way to its final state
(`EMISSIVE_STEP`, noise floor subtracted).

Response to a light change: `--sun_step=factor` multiplies the sun energy
as the measurement starts. Converge first, then trace the GI buffer:
`--view=interior --tod=0 --anim=0 --debug=1 --frames=400 --measure=300
--trace_mean=1 --sun_step=0.5`, and count the frames to 90% and 98% of the
change. This is reproducible to a few frames; the first 100-200 frames
after startup are not (asynchronously compiled hit groups change what the
first probe rays see, so the image mean at frame 20 varied from 10 to 30
between identical runs).

Baking: `--volume=cx,cy,cz,sx,sy,sz` adds a `DDGIVolume` (with
`--vspacing=`, `--vcascades=`), `--bake=path.res` bakes and saves it before
the capture, `--baked=path.res --bake_mode=0|1|2` loads one, `--bake_ls=0`
bakes without the least-squares refinement.

```
bin\godot.windows.editor.x86_64.console.exe --path misc\ddgi_test_project -- --view=interior --tod=0 --anim=0 --res=640x360 --linear --exposure=4 --frames=600 --measure=60 --stdmap=C:\tmp\std.png
bin\godot.windows.editor.x86_64.console.exe --path misc\ddgi_test_project -- --view=interior --tod=0 --anim=0 --volume=400,1.5,0,13,3.6,11 --vspacing=0.5 --bake=C:\tmp\interior.ddgi.res --frames=1 --measure=2
bin\godot.windows.editor.x86_64.console.exe --rendering-driver d3d12 --path misc\ddgi_test_project -- --view=interior --tod=0 --anim=0 --volume=400,1.5,0,13,3.6,11 --vspacing=0.5 --baked=C:\tmp\interior.ddgi.res --bake_mode=1 --frames=10 --measure=30
```

Keys: `1`-`4` views, `G` GI mode, `U` quality, `Tab` debug mode, `L`
animation, `M` camera path, `P` path tracing; fly camera with the right mouse
button and WASD/QE.

## Benchmarks

Measured with the benchmark harness above. GPU: see each run. Times are
averages over the measured frames, after warm-up.

RTX 3060, Vulkan, 1920x1080 native, 300 warm-up + 300 measured frames,
`--gpu-profile`, measured 2026-10-11 after the bounce light pass (step 20).
Default volumes (3 cascades of 24 x 12 x 24, 1 m spacing). "Frame" is the
whole frame's GPU time; "DDGI" is the sum of the DDGI passes, "Blend" the
irradiance and distance blends together. All times in ms.

| View | GI | Frame | DDGI | Trace | Bounce | Blend | Smooth | Apply |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| room | Low | 2.43 | 1.07 | 0.06 | 0.02 | 0.09 | 0.09 | 0.66 |
| room | Medium | 2.70 | 1.34 | 0.14 | 0.07 | 0.22 | 0.10 | 0.65 |
| room | High | 3.24 | 1.89 | 0.30 | 0.14 | 0.48 | 0.15 | 0.66 |
| room | Ultra | 4.33 | 2.97 | 0.66 | 0.30 | 1.03 | 0.15 | 0.67 |
| outdoor | Low | 2.12 | 1.15 | 0.08 | 0.07 | 0.10 | 0.09 | 0.66 |
| outdoor | Medium | 2.63 | 1.65 | 0.22 | 0.25 | 0.30 | 0.10 | 0.63 |
| outdoor | High | 3.78 | 2.82 | 0.63 | 0.54 | 0.74 | 0.15 | 0.62 |
| outdoor | Ultra | 6.57 | 5.60 | 1.51 | 1.34 | 1.80 | 0.15 | 0.63 |
| stress | Low | 2.48 | 0.92 | 0.06 | 0.03 | 0.09 | 0.09 | 0.50 |
| stress | Medium | 2.77 | 1.22 | 0.16 | 0.10 | 0.23 | 0.10 | 0.48 |
| stress | High | 3.36 | 1.80 | 0.36 | 0.18 | 0.47 | 0.15 | 0.49 |
| stress | Ultra | 4.89 | 3.36 | 0.89 | 0.48 | 1.18 | 0.16 | 0.49 |

The same runs before step 20 (bounce light looked up in the closest hit),
DDGI ms Low / Medium / High / Ultra: room 1.13 / 1.46 / 2.05 / 3.32,
outdoor 1.22 / 1.98 / 3.82 / 8.00, stress 0.93 / 1.28 / 2.08 / 3.82; the
trace alone was 5.10 ms in outdoor Ultra (now 1.51 + 1.34 bounce).

The variants below are older (commit `835a780893`, before the smooth
pass, guided rays and the temporal gradient); the relations still hold.

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

- 2026-10-12: Performance and stability pass. Baseline (RTX 3060, 1080p,
  DDGI ms Low / Medium / High / Ultra): room 1.40 / 1.72 / 2.21 / 3.48,
  outdoor 1.34 / 1.92 / 3.25 / 6.26, stress 1.27 / 1.59 / 2.19 / 3.70. The
  apply chain is a fixed 0.8-1.0 ms at every quality; split with new
  profiler sub-passes (room, Medium): low resolution sampling 0.33, near-field
  trace 0.20, upsample 0.25, near-field filter 0.08, resolve + temporal
  0.10, mark 0.02. Changes: the GI reflection buffer only holds a constant
  (alpha -1), so it is written once (again after a resize or when another GI
  wrote it), not every pixel every frame; the upsample loads only the 4x4
  blocks it uses instead of 10x10. Upsample 0.24 -> 0.22 ms: it is bound by
  the full resolution reads and writes, not the block loads. Tried and
  dropped: skipping the distance blend for converged probes whose fixed rays
  saw no change (3 of 4 updates). Distance blend -65% (outdoor Ultra 0.95 ->
  0.29 ms), but the tunnel got 2.6% brighter (0.0374 -> 0.0384, spread
  0.0001) with flicker std 0.22 -> 0.34, and realtime interior flicker
  pixels 0.88% -> 1.72%; a build without the skip matched the baseline.
  Stability: new `--chaos` test, 5 seeds x 60-80 changes: no errors, no
  leaks, no crash; with the Khronos validation layer (sync validation on)
  no message names a DDGI resource. GI buffer against the baseline: mean
  difference under 1 level, no pixel off by 8. Storm level unchanged.
  Also tried and dropped: the smooth pass as one workgroup per probe,
  skipping probes inside geometry and probes whose display copy had caught
  up (1/1000 of the gap): nearly every probe updates within the 83 frames
  that takes at the default transition time, so little was skipped, and
  10x10 tiles left 8x8 workgroups half idle (Ultra 0.16 -> 0.25 ms). Not
  tracing the moving objects' row of near-field rays when there are none:
  no measurable change (0.10 ms either way).

- 2026-10-12: Report: white speckles on walls near a bright tunnel
  opening, crawling while walking ("light flowing like water"); not in
  0.5.0/0.6.0. Present since 0.7.0 and also with near-field rays off;
  not with the full resolution apply. Ruled out step by step (camera walk
  past the wall, difference against full resolution): normals of the
  material, the min/max checkerboard, the per-pixel normal gradient,
  depth and plane matching, render graph ordering, volumetric fog. Raw
  block values were smooth; bilinear-only upsampling was clean; forcing
  the GI coverage (alpha) to 1 was clean. Cause: the upsample blended the
  coverage with the per-pixel match weights; at single pixels it dipped
  below 1 and the scene shader filled the rest with the (much brighter)
  sky ambient. Coverage is now blended bilinearly only. Flicker unchanged
  (0.00%), bleed shapes unchanged.

- 2026-10-12: Step 30, continued. Report: screenshots of the game level's
  crouch tunnel (probe spacing 2 m) with soft blotches and specks. Probes
  only: clean. Found and fixed, each checked in the tunnel (static, and the
  camera walking through) with a fixed camera in the level:
  dark specks on the floor came from scaling the probes' per-direction
  gradients by near-field / probe light (up to 4x where the probes are
  nearly black): removed. Soft blotches: the rays reached 2 spacings (4 m)
  and caught the sunlit ground outside only now and then; their average
  stayed patchy after 128 frames. The noise gate now uses temporal moments
  of the samples (as SVGF), a factor 3, and runs before the blur (after
  it, neighboring blocks flipped and showed as speckle), but the fix that
  removed the blotches was the range: `near_field_distance` 2 -> 0.5
  spacings (moving objects keep 2 spacings with their own rays). Also: back
  face hits (rays starting inside overlapping geometry) count as misses;
  the "light changed" reset needs a 30% change (5% fired on probe noise).
  Results: tunnel clean at 1 m and 2 m spacing, also walking; bleed shapes
  keep their red bounce; flicker pixels 0.00%; storm level settles at once,
  GPU time about 2 ms (was 4.4).

- 2026-10-12: Step 30, noise in dark places. Report: blotches and noise
  in a dark tunnel of the game level with 0.7.1. New `tunnel` view.
  Flickering pixels (realtime updates): 0.6.0 0.01%, 0.7.1 1.39%; with
  near-field rays off 0.00%. Cause: the near-field history was clamped
  every frame to this frame's neighborhood, and in the dark only a few rays
  reach the sunlit opening, so that neighborhood jumps. Now the clamp runs
  only near moving objects or when the probes' light at the block moved
  more than 5% from its moving average; otherwise up to 128 frames. The
  history holds the near-field light minus the probes' (the correction),
  so the result follows the probes at once (with the light itself, the
  storm level took about 110 frames to settle after the lamp stopped; now
  0). A noise gate keeps the probes' value where the near-field differs by
  less than its remaining standard error. Thin bright streaks along the
  tunnel's ceiling corners: near-field hits a few cm away looked the probes
  up where they leak from outside; close hits (under half a spacing) now
  blend to the block's own probe light. Results: flicker pixels 0.00% in
  tunnel, interior and bleed; tunnel std 0.17 (static) / 0.64 (realtime,
  as 0.6.0); storm settled 21.0, 3 runs within 0.01. A wide clamp (6
  standard errors) brought 0.02% flicker back for a small speed-up and was
  dropped. The storm test now turns rain off (particles and wet shaders
  differed between runs).

- 2026-10-12: Step 29, moving objects of any shape (0.7.1). Moving objects
  were spheres of their mean size. Now each moving surface is its oriented
  bounding box (from the mesh's local bounds, read back once per mesh with
  its surface area): the form factor is exact for the box's visible faces
  (Lambert's formula, clipped at the horizon), times how much of the box the
  mesh covers on average (mesh area / box area: mean projected area is a
  quarter of the surface area for convex shapes). Sphere-like meshes (equal
  sides, area ratio near pi / 6) keep the exact sphere formula. Tested with
  `--shape=ball|box|pole|plank|capsule` against the path tracer: red on the
  wall at the sunlit side for ball, box and capsule, faint above the plank,
  almost none for the thin pole, as in the path tracer. Problems on the
  way: points on an object's own face weren't inside its box (no margin), so
  objects lit themselves (now: closer than 2 cm skips the object); one ray
  per block jumped between a lit and a shaded face (hard-edged patch under
  the box); three rays per block (one per face) cost 1.4 ms in the room. Now
  one ray per block only tells whether the object is in view, and every
  ray's hit adds to an average per object side (six bins, by the hit
  normal along the object's axes, last frame's used). The exact polygon
  form factor with acos cost another 0.9 ms: now only for the picked
  object, no clipping when the face is above the horizon, and a rational
  fit for angle / sin (which includes the 1 / 2 pi: dividing again made
  boxes 6x too weak at first). DDGI in the room 1.62 -> 1.78 ms; static
  flicker and the storm level unchanged. Also: ray tracing binding 0 got a
  rgba16f default image in DDGI dispatches but is rgba32f (14 validation
  messages per editor smoke run); it now has its own default.

- 2026-10-12: Step 28 (branch `dev-ddgi-quality`). Report: screenshots
  of the red ball scene with a red glow beside and under the ball, and
  thin orange lines (those were editor gizmo lines: a game run has none).
  Measured against the path tracer at a fixed ball position (`--jitter`
  keeps it a moving object), red excess beside / above / under the ball
  and brightness relative to the far wall:

  | | Beside | Above | Under (brightness) |
  | --- | --- | --- | --- |
  | Path tracer | -12.6 | -7.2 | 0.59 |
  | Step 27 | -2.9 | +3.1 | 0.74 |
  | Object bounce off | -12.7 | -7.7 | 0.74 |
  | + hidden light toward the object | -12.7 | -7.7 | 0.66 |

  The ball's light included the probes' bounce light at its surface; the
  probes don't see the ball, so under it they count the floor as lit, and
  the ball's shaded underside came out bright red. The object's direct
  light alone matches (`NF_OBJECT_BOUNCE` 0). The light it hides is now the
  probes' irradiance facing the object (was the surface's own, which
  averages the whole hemisphere). Beside the ball the wall is still lighter
  than in the path tracer (0.93 vs 0.80). A shadow edge across the ball
  (the wall's) showed as a straight-edged pink wedge on the wall: each
  block's ray sees one point of the ball. The object light now goes through
  the two filter passes (not the temporal accumulation). Static red objects
  (not moving) still show too much red right under them: the near-field
  hits take the probes' leaking light there.

- 2026-10-12: Step 27, noise near moving objects (branch
  `dev-ddgi-quality`). Report: after step 26 the light around the ball was
  grainy ("needs to be smooth, no noise allowed"). One cosine ray per block
  hits the ball only now and then, and the short history near it can't
  average that out. Sampling the cone toward the ball half of the time
  (mixture pdf weights) didn't help visibly. Fix, as for the emissive
  lights: the cosine rays skip moving objects too (`rt_cull_mask()` 0x7F
  for every DDGI dispatch); each block traces one more ray, toward the
  center of the nearest moving object (spheres: center and mean half
  extent, from `RenderRaytracing`), and adds (light leaving the hit -
  probe irradiance) x the sphere's form factor (the emitters'
  `sphere_light()`), faded out over the second half of the range. It is
  the same every frame, so it isn't accumulated. First try used half the
  box diagonal as radius: the ball counted 1.7x bigger and the surfaces
  inside that sphere showed a ghost outline and a flat patch. Result: the
  ball's red bounce and shadow are smooth and follow it in both the bleed
  view and `bleed.tscn`. Interior flicker std 0.069-0.073 (unchanged);
  storm level settled GI 20.9 (was 21.2: small moving objects now skip the
  probes); DDGI 1.50 -> 1.62 ms (room 1080p, which has a moving ball and
  box).

- 2026-10-12: Step 26, moving objects (branch `dev-ddgi-quality`). Report:
  in the user's red ball scene (`misc/ddgi_test_project/bleed.tscn`,
  animated) the indirect light had ugly smears and blobs, "almost like bad
  AO": a dark blob and red streaks where the ball had just been. Cause: the
  probes saw the ball and blended it in and out over many frames, and the
  near-field rays that hit nothing took that lagging probe light. Fix,
  after the emissive lights: probes don't see objects that keep moving
  (moved on 3 frames, until still for 60; TLAS instance mask 0x80,
  `rt_cull_mask()` 0x7F for probe dispatches and their shadow rays;
  emissive objects stay), setting `skip_moving_objects`. Near-field rays
  that hit a moving object mark their block; there and on the next frame
  the history clamp is 2 instead of 4 standard errors and the history at
  most 8 frames. (The first try passed the mark on through the history: it
  never cleared and the whole wall went noisy.) Result: the smear is gone,
  shadow and red bounce follow the ball; static flicker unchanged
  (interior std 0.072/0.074), storm level unchanged (GI 12.6-13.1 /
  21.2-21.3). The red near the ball is a little grainy in the bleed view.

- 2026-10-12: Step 25, near-field rays (branch `dev-ddgi-quality`). Report:
  ugly blobs and spots in the indirect light; it should be smooth and fast
  like the path tracer. New `bleed` view (red ball, white wall). Probes
  only: the red on the wall is a round blob around the nearest probes,
  above and behind the ball, trailing it. Realtime updates and 0.5 m
  spacing changed the frames by less than 1 level (the lag is the
  blending); hysteresis 0.5 without display easing followed faster but
  made every surface blotchy. Near-field rays (above, Architecture 7):
  the red sits on the ball's sunlit side on wall and floor, as in the path
  tracer, and follows the ball. First version: far field from the probes'
  irradiance at the ray's end made the floor too bright (its cosine lobe
  saw the sunlit floor beside the ray); the probes' irradiance at the
  block fixed it. Clamp 2.5 -> 4 standard errors and blend 0.1 -> 0.06:
  interior flicker std 0.095 -> 0.07, pixels 0.34% -> 0.07%. RTX 3060,
  Medium:

  | | Probes only | Near-field rays |
  | --- | --- | --- |
  | DDGI ms, room 1080p (2 runs) | 1.148 / 1.117 | 1.504 / 1.498 (apply 0.40 -> 0.78) |
  | Interior flicker std / pixels (2 runs) | 0.046, 0.056 / 0.00, 0.03% | 0.067, 0.073 / 0.07, 0.07% |
  | Bleed, static, flicker std | 0.120, 0.118 | 0.145, 0.145 |
  | GI mean interior / room | 13.92 / 151.63 | 13.18 / 145.53 |
  | Storm game level (3 runs), GI mean moving / settled, jitter | 12.77 / 21.32, 0.093 | 12.80 / 21.24, 0.093 |

  Smoke tests pass. Also checked: emissive lights (step before) give a
  smooth light pool from the moving sphere in the storm level.

- 2026-10-12: Step 24, least-squares bake (branch `dev-ddgi-quality`).
  Each case baked at 0.5 m spacing, rendered baked-only, compared with an
  independent plain bake at 0.25 m (the reference), 1280x720:

  | Scene | Plain: mean diff / p99 | Least squares | Interpolation error (fine probes) | Solve |
  | --- | --- | --- | --- | --- |
  | Interior, 1 cascade | 1.341 / 11.7 | 0.738 / 7.3 | 0.0243 -> 0.0123 | 505 ms, 21498 fine probes |
  | Room (emissive panels), 2 cascades | 0.721 / 10.3 | 0.471 / 9.3 | 0.0369 -> 0.0210 | 372 ms, 11351 fine probes |

  Brightness unchanged (26.18 vs 26.27 interior). The refined bake also
  shows less blotchy noise than the reference. Bake time 0.5 -> 6.0 s
  (interior). Smoothness 0.05 and prior 0.05 were the first values tried,
  not tuned.

- 2026-10-12: Step 23, low resolution apply (branch `dev-ddgi-quality`,
  from the GI downscaling article in `kb/research/gi-downscaling.md`).
  RTX 3060, 1080p room, apply pass (includes the 0.03 ms mark pass):

  | apply_resolution | GI buffer full | GI buffer half |
  | --- | --- | --- |
  | Full | 0.717 | 0.192 |
  | 1/2 | 0.667 | 0.484 |
  | 1/4 (default) | 0.399 | 0.198 |
  | 1/8 | 0.362 | 0.158 |

  DDGI total 1.400 -> 1.131 ms at 1/4. Interior GI buffer against full
  resolution, converged: mean absolute difference 0.28-0.37 levels (p99
  1.7-2.7), brightness the same. Stress view (many small boxes): 1.13
  levels at 1/4 (p99 10), 1.83 at 1/8 (p99 21, edges of small far
  objects). Outdoor motion error 0.886-0.893 (full 0.893). Flicker in the
  same range (3 runs each). Where the time went, in order of finding it:
  - The first build was 12% too dark with banding: the block normal was
    decoded with `oct_to_vec3()`, which expects -1..1, from
    `vec3_to_oct()`, which returns 0..1.
  - Reading all pixels of a block to average them cost as much as a full
    resolution read pass (0.15 ms); 1/8 was slower than 1/4. Now 2x2
    pixels per block.
  - The five directions cost about 0.02 ms over one (probe weights are
    shared). Shared memory for the upsample changed nothing: its cost is
    the full resolution reads and writes, not the block fetches.
  - The min/max checkerboard reference (from SDFDDGI) instead of the
    center pixel: same quality (stress view p99 10.3 vs 10.0 levels at
    1/4), 4 reads per block instead of 5: apply 0.372 ms at 1/4, 0.309 at
    1/8. Open: about 0.1% of the stress view's pixels (edges of thin far
    boxes) are far off at 1/4 and 1/8 (p99.9 92-165 levels), where no
    block around the pixel matches its surface.

- 2026-10-12: Step 22, scheduling (branch `dev-ddgi-sched`). Probes the
  surfaces actually sample now get the highest rate instead of probes whose
  position is in the view frustum (idea from Ghost of Yotei, see
  `kb/research/ghost-of-yotei-ambient.md`). RTX 3060, interior, sun halved
  after 400 frames, GI buffer mean, 2 runs each:

  | Variant | 90% of change | 98% | Overshoot | Outdoor motion error | Apply ms (1080p room) |
  | --- | --- | --- | --- | --- | --- |
  | Before (frustum: in view 2x, else 0.6x) | 230, 231 | 272, 273 | 2.2-2.7% | 1.000 | 0.662 |
  | Marked 2x, frustum 1x | 219, 223 | 275, 275 | 2.4-3.4% | 0.916 | 0.698 |
  | Marked 3x, frustum 0.8x, marked in apply | 208, 217 | 266, 270 | 1.2-1.8% | 0.909 | 0.708 |
  | Same, separate mark pass, 16 frame window | 202, 207 | 269, 272 | 1.7-2.6% | 0.919 (4 frame window) | 0.69-0.70 |
  | Marked 2x, frustum 0.8x, mark pass (shipped) | 211, 215 | 264, 267 | 1.6-2.0% | 0.937 | 0.675 |

  Outdoor motion error: mean absolute difference between the GI buffer while
  flying (`--move=1`, 300 frames) and after settling 300 frames. Static
  interior flicker (600 + 60 frames, 3 runs): before 0.01/0.06/0.01% of
  pixels, std 0.156-0.172; after 0.06/0.03/0.04%, std 0.163-0.191. Visible
  probes update more often, so their update noise shows slightly more.
  Marking inside the apply pass cost 0.05 ms whether every pixel, one per
  4x4 block or a rotating quarter of the workgroups marked: the code alone
  (divergent warps, registers) cost it, compiled out it was back to 0.662.
  A separate pass, one thread per 8x8 tile, costs about 0.03 ms. With a 4
  frame window, two runs flickered on 0.07% and 0.49% of pixels.

  Correction, same day: with 6 flicker runs per build the 16 frame window
  was not the fix. Before: std 0.155-0.177, 0.01-0.19% of pixels (the
  baseline has outliers too). 3x: std 0.179-0.203, 0.00-0.61%. 2x
  (shipped): std 0.155-0.177, 0.00-0.11%. More updates of the visible
  probes add their update noise; 2x keeps most of the speed. The DDGI
  cost of the shipped variant is 1.317-1.335 ms (before 1.329).

  Tried and rejected for moving lights (light trailing a moving lamp):
  - Display smoothing that follows large gaps fast (above
    max(0.2, 2x noise), then max(0.15, 0.5x noise)): emissive step 50%
    after 17-25 frames instead of 26-29, 90% unchanged (63-72), but
    the weather fade showed probes stepping one by one (worst frame:
    0.1% of pixels jump 8-10 levels, before 1).
  - Flagging the 6 neighbors of a probe with a large confirmed change
    for the next frame: in the storm game level the settled GI level
    varied 17.4-27.6 between runs (before 21.4-22.8), with stale dark
    blocks and sky-lit walls in some runs.
  A real game level (storm weather, a bright emissive sphere circling in
  front of the camera, baked + realtime updates, auto exposure off,
  `--fixed-fps 60`, 3 runs): GPU time unchanged within noise (3.28-3.43 vs
  3.37-3.89 ms), GI buffer mean while the sphere moves 20.2-20.8 (before
  17.9-21.0), jitter the same. Single frames there differ between runs
  even with a fixed frame rate, so only averages are compared. Test method:
  `kb/tooling/ddgi-test-protocol.md`.

- 2026-10-11: Step 20, performance. Measured first: the DDGI irradiance
  lookup at the hit of each probe ray (8 probes, two texture reads each)
  took about 70% of the trace time (outdoor Ultra 5.10 ms with it, 1.48 ms
  without), as the ray tracing pipeline runs it at low occupancy. The
  closest hit now writes albedo, normal and the hit point (direction and
  distance from the probe, so rays through glass work too) into a
  16-byte record per ray (`DDGIRayBounce`, binding 40), and a compute pass
  after the trace (`MODE_BOUNCE`) does the lookup. Outdoor Ultra trace +
  bounce 1.51 + 1.34 ms instead of 5.10; DDGI 8.00 -> 5.60 ms, Medium
  1.98 -> 1.65 ms; every view and quality got faster. Same image: mean
  linear brightness equal to 4 digits in all four views, per pixel
  difference of the mean images within the run-to-run difference of the
  old build (interior mean 0.45 vs 0.61 levels, stress max 18 vs 15).
  The distance blend now passes over the rays once for all (up to 2 x 2)
  texels of a thread: 4-7% off the blends only, it is bound by the cos^50
  arithmetic, not by the shared memory reads. Tried and dropped: skipping
  smooth pass texels of probes whose display has settled (90 frames after
  their update): with the budget feedback every probe updates sooner than
  that, so nothing was skipped; the pass is bound by memory bandwidth
  (about 48 MB per frame at Ultra).

- 2026-10-11: Step 19. Report: dark spots (inside the demo's crouch tunnel)
  stay lit and bluish with DDGI, black with the path tracer. Measured with
  fixed exposure (auto exposure hides it: the path tracer's unconverged
  fireflies hold the exposure down, DDGI's smooth dark image gets raised):
  tunnel near its 1.1 m opening, DDGI 0.496 against path tracer 0.012
  (mean linear-ish luminance of the lower screen). Without sky reflections
  DDGI was 0.052: about 90% was sky and reflection probe light reflected
  inside the tunnel, which DDGI didn't touch and Godot's specular occlusion
  (from the ambient brightness) only halves. Fix: DDGI marks its pixels
  with reflection buffer alpha -1, and the scene shader multiplies indirect
  specular light by DDGI irradiance / environment ambient light (clamped to
  0..1). After: 0.064 near the opening; deeper in the tunnel DDGI 0.0025 /
  0.0073 against path tracer 0.0032 / 0.0053; outdoors 0.553 against
  0.537. The rest near the opening is probes in and outside the opening
  spreading outdoor light about a spacing inward (denser probes don't
  help; not the bounce count: path tracer with 8 or 16 bounces 0.014).
  Interior flicker 0.26%, DDGI time unchanged.

- 2026-10-11: Step 18. Report: after step 16, outdoor faces in sun shadow
  were pitch black, in a voxel world (infinite world) and in a grid-shader
  test level (fixed volume); the probe states view showed many probes in
  open air as inside or inactive. Cause: surfaces with a custom
  ShaderMaterial were left out of the TLAS until their hit group finished
  compiling, which takes seconds (the voxel terrain had no geometry in a
  path traced view at 400 frames, all of it at 900). Probes classified in
  that time saw no geometry: inactive, or inside where they saw the back of
  something; inactive and inside probes update at an eighth of the rate, so
  the states stayed wrong long after. Step 16 then made "no usable probe"
  dark instead of the sky ambient light. Fixes: (1) such surfaces are traced
  with the default hit group and a grey material until theirs is ready
  (`_rt_hit_group_pending()` in render_raytracing.cpp); (2) inactive probes
  are sampled with 5% weight (they keep their light current with the fixed
  rays), so only probes inside geometry make a point dark. Checked: voxel
  world after 7 s and 15 s and the grid level, clear and storm: no black
  faces; the tunnel test stays dark; interior flicker 0.02%, mean unchanged.
  A moving emissive sphere costs nothing measurable (5.3 vs 5.4 ms GPU;
  `--write-movie` runs report 30-80 ms GPU time, ignore those).

- 2026-10-11: Step 17. Report: GI around moving objects lags (bridge scene
  with a moving sphere, rotating cube, realtime updates). New ghost test
  (emitter moved away from a wall, DDGI indirect view): half the light
  gone after 28 frames, settled after 82. Forcing the hysteresis to 0 where
  probes had history gave 11 / 33, the floor set by the display easing.
  Temporal gradient, three tries: (1) relative change of the fixed rays'
  light minus twice the texel noise: no effect, the noise estimate of a
  texel lit by a small bright source is high exactly when the change is.
  (2) Without the noise term: static interior flicker 0.03% to 4.2%; the
  fixed rays carry shading noise between updates (soft shadow edges). (3)
  Per ray "moved" from the hit distance, weighted by the ray's light, per
  texel and per probe: 15 / 54 frames, flicker unchanged. Cost: blend +0.03
  ms budgeted, +0.14 ms realtime; packing the history into halfs and
  skipping the texel loop when nothing moved didn't change that much (the
  cost is mostly the history traffic). Guided rays: interior noise -18%,
  realtime interior flicker pixels halved, +0.06 ms (+0.18 ms realtime),
  mean brightness unchanged (no bias). `DDGIProbe` grew to 80 bytes
  (`updates`, `history_rays`, `guide`, `guide_next`).

- 2026-10-11: Step 16. Report: in a voxel world, dark caves turned blue,
  and lighting popped while moving with `infinite_world`. Reproduced with a
  2 x 3 m dead-end tunnel through a solid block, auto exposure on. Two
  leaks: (1) volumetric fog injected the unoccluded sky ambient light into
  every froxel, so the tunnel filled with blue haze (the main one); (2) when
  all 8 probes around a point were inside the rock, the volume returned
  alpha 0 and the scene shader fell back to the environment's sky ambient.
  Fixes: such points count as black at the volume's weight (probes never
  traced still leave the point uncovered); a `DENSITY_WITH_DDGI` volumetric
  fog variant samples the probes isotropically (up + down irradiance, same
  visibility weights) and replaces the ambient light where they cover the
  froxel. After: no haze in the tunnel, the probes' indirect light there is
  black. Popping: the scroll test showed colored blotches near the moving
  edge. The first update of a scrolled-in probe was kept at 0.95 hysteresis
  for dozens of updates. Added `DDGIProbe.updates` (struct 32 to 48 bytes):
  young probes average their first updates and get 4x the update rate
  until 8. Scrolled-in probes are no longer sampled before their first
  trace. Scroll test (outdoor, 8 m/s): mean error 3.2 to 2.9, pixels off by
  more than 16 levels 3.4% to 2.75%. Static interior flicker 0.01%. In a
  voxel world, far terrain past the last cascade gets the plain ambient
  light: use enough cascades to cover the view distance.

- 2026-10-09: Step 15, smooth light changes. Report: switching a weather
  system to a storm showed the probes updating one by one, blotchy, and
  lagging. Measured with `--weather` (indirect light view, every frame
  saved): the frame-to-frame change showed dark blobs where single probes
  jumped. Each probe sees a real change at its own update; once confirmed,
  the hysteresis drops to 40% and the probe jumps 60% of the way. Tried:
  (1) realtime updates alone (every probe every frame, 48 rays): even, but
  noisy (static interior 6.5% flicker) and only a little faster, because
  the per-update hysteresis then sets the pace. (2) A display atlas that
  eases toward the probes every frame: hides the steps; first with a time
  in seconds, which at the harness's 500 fps meant 100 frames, so it is
  counted in frames (seconds at 60 fps). (3) Reacting to every confirmed
  change in proportion (no 0.1 threshold): faster, but darker in rooms lit
  by a small source (switch test settled at 95.5 instead of 104): with
  skewed noise two small dips in a row are common, two spikes rare.
  Reverted. Kept: the display atlas (0.2 s), realtime updates with 64
  rays, per-update hysteresis 1 - 2 x (1 - h), distances every 4th frame.
  Numbers in "Smooth light changes". Scrolling got a little worse (outdoor
  at 8 m/s: mean error 3.08 to 3.25, pixels off by more than 16 levels
  3.0% to 3.6%): the easing adds lag while moving. Considered and not done:
  keeping the sun's and the sky's light as separate, normalized atlases,
  so their brightness and color would apply at once without tracing: three
  irradiance atlases, three blends and three lookups per probe.

- 2026-10-09: Bake errors in projects with DLSS or FSR as the default 3D
  scaling ("slEvaluateFeature ... eErrorNGXFailed", "Too many mipmaps
  requested", "Dispatch amount of X compute threads (0) is zero", invalid
  textures). The bake's offscreen view takes the project's scaling
  defaults, and step 14 had made it 16x16: DLSS at about 10x10 pixels
  fails, and post effects need a few mip levels. The view now renders at
  native resolution without upscaler or anti-aliasing, at 64x64.
  Reproduced with the harness's `--ps_scale3d=6` (100 errors); with the
  fix none for DLSS, FSR2 and bilinear, bake time unchanged (0.53 s).

- 2026-10-09: Step 14. `DDGIVolume.probe_count` (probes per axis, linked to
  `size`; the spacing stays the same on every axis, so the shaders didn't
  change) and `infinite_world` (the former `follow_camera`, never
  released; `_set()` still accepts the old name). Bake speed: the bake was
  bound by the fixed cost per frame, not by ray tracing. With the realtime
  budget (2048 probes per frame at Medium) a 20736 probe volume took 1296
  frames: 2.39 s at Medium, 1.78 s at Ultra (RTX 3060, no editor views). Now
  it traces up to 16384 probes per frame (a temporary Custom quality with
  the preset's rays and texels), 96 updates instead of 128 with a running
  average, a 16x16 view, and the editor's 3D views paused: 1.16 s and
  1.52 s, now bound by the rays. Interior: 895 to 535 ms, and closer to a
  converged Ultra reference (mean difference 3.21 to 2.33 levels, pixels
  off by more than 4 levels 20% to 11%). Also found: probes per frame above
  16384 couldn't work (one ray data row per probe, textures stop at 16384);
  the setting now stops there.

- 2026-10-09: Two errors while baking in the editor. (1) "Failed to call
  streamline slSetConstants. Result: sl::eErrorDuplicatedConstants": the
  bake renders hundreds of frames with `RenderingServer::draw()` inside one
  main loop iteration, but the Streamline frame token only advances at the
  start of an iteration, so every DLSS viewport set its constants twice for
  one token (and skipped DLSS for those draws). `DLSSEffect::upscale()` now
  takes a new token when the viewport's constants were already set for the
  current one. Bake of the interior with DLSS on: 345 errors before, 0
  after. (2) "Another resource is loaded from path ... (possible cyclic
  resource inclusion)" on a rebake: the new `DDGIProbeData` got the path of
  the old one, which was still loaded. The Bake DDGI button now updates the
  loaded resource in place (and a new one takes the path over).

- 2026-10-09: Steps 11 (interiors) and 12 (baking). New `interior` view and
  `--measure` (flicker and brightness), all numbers below at 640x360,
  Medium, static scene (`--anim=0 --tod=0`), linear exposure 4, RTX 3060.
  **Flicker.** Before: 29% of the pixels deviated by more than 2 levels from
  frame to frame (mean deviation 1.68 levels), with the light unchanged.
  `--trace_px` on a flickering spot showed the cause: now and then one
  update jumped (35 to 57 levels) and decayed over a few updates. A few rays
  hitting the small sunlit patch (or the sky's sun disk through the window)
  carry most of a dark probe's light; such an update passed the "light
  changed" test, the hysteresis dropped to 40%, and the spike went in at 60%.
  Relocation, classification and the visibility test had nothing to do with
  it (switched off one by one, no change). Fixes, in
  `ddgi_update.glsl` and the miss shader: (1) only the change above two
  standard errors of the update's ray mean counts as a light change; (2) a
  change counts only when the next update changes the same way
  (`DDGIProbe.pending_change`, the former unused `luminance`); a probe with
  a pending change is traced again the next frame, so this costs one frame;
  (3) each texel tracks its own noise (standard error of its cosine lobe,
  a slowly decaying maximum kept in the irradiance atlas alpha) and noisy
  texels blend in with down to half the weight; (4) probe rays that miss
  read the sky at a blur matching the ray footprint (4 pi / rays sr), not
  at mip 0, so the sun disk is spread over the rays near it. After: 0.35%
  to 0.9% of the pixels (four runs), mean deviation 0.33 to 0.36. Ultra:
  18% to 0%. With a `DDGIVolume` at 0.5 m spacing, 1.4% to 1.7% (more
  probes, each updated less often); baked, 0%.
  **Brightness.** The old adaptive hysteresis was biased upward in noisy
  scenes: bright spikes changed the probe more (relatively) than dark dips,
  so they got more weight. In the room light switch test (one small panel
  left) the old code settled at a mean of 126 (tonemapped), the new one at
  104; the new one has a fixed weight when nothing changed, so it is
  unbiased. That makes such scenes darker than before. The interior compared
  with the path tracer: DDGI 0.0455 vs 0.0407 (path tracer, 16 spp, 120
  frames, 3 bounces), so DDGI doesn't lose light. Blending probes linearly
  instead of in square root space changed the mean by 2% only, so that
  stayed. For brighter interiors: `bounce_energy` (multi-bounce feedback,
  capped at 0.95 per channel when above 1): 1.5 gives +10%, 2.0 +28% in the
  interior view, with less flicker (0%).
  **Cost of the stability.** Reaction to big changes is slower: after the
  light switch the room needs about 80 frames to get within 9 levels of
  its final state (before: about 20, but to a biased, blotchy result).
  Small slow changes take longer too in noisy probes. Scrolling got a
  little better (outdoor at 8 m/s: mean error 3.44 to 3.08, pixels off by
  more than 16 levels 3.5% to 3.0%). GPU time unchanged or lower (room
  Medium DDGI 1.26 to 1.19 ms, outdoor 1.79 to 1.73 ms at 1080p).
  **Baking.** `DDGIVolume.bake()` renders a 64x64 offscreen view of the
  world for 128 updates per probe (second half at hysteresis 0.98), reads
  the atlases and probe buffer back (`RenderingServer.viewport_get_ddgi_probe_data`)
  into a `DDGIProbeData`. `RenderDDGI` uploads it into the atlases when it
  matches the volume (cascades, grid, spacing, center) and the baked probes
  count as updated, so dynamic updates blend into them. Baked only skips
  the TLAS, the trace and the wide ray tracing cull set, and runs on the
  D3D12 driver. Interior: bake 965 ms, 4.7 MB; first frames with Baked
  0.0457 vs settled dynamic 0.0466, no flicker (0.00%), same image on
  Vulkan and D3D12. Editor: Bake DDGI toolbar button
  (`editor/scene/3d/ddgi_volume_editor_plugin.cpp`), saves next to the
  scene and switches Dynamic to Baked + Dynamic. The button itself wasn't
  clicked in a test (no UI automation); the bake path it calls was.

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
