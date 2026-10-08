# Path tracer test and fix plan

Working notes for making the NVIDIA path tracer (`RenderForwardClusteredPT`)
handle PBR materials, lights, emission, custom shaders and fog natively. This
file is the shared memory for humans and agents working on it: read it before
starting, and update the step table and the findings log as you go.

Work happens on the `dev-pt` branch. A finished step (build, smoke tests,
screenshots) is merged into `dev`; see [CUSTOM_BUILD.md](CUSTOM_BUILD.md).

## How to run the test scene

The test project is [`misc/pathtracer_test_project/`](misc/pathtracer_test_project/).
It builds every test area from code (`main.gd`), so there is no scene file to
keep in sync. The path tracer needs Forward+ and the Vulkan driver; the
project sets both.

Interactive:

```
bin\godot.windows.editor.x86_64.exe --path misc\pathtracer_test_project
```

Keys: `1`-`8` switch views, `P` toggles path tracing, `D` cycles the debug
mode, `R` toggles DLSS Ray Reconstruction, `F` toggles volumetric fog.

Screenshots (for comparing path traced against raster, or before against
after a fix):

```
bin\godot.windows.editor.x86_64.console.exe --path misc\pathtracer_test_project -- --view=pbr --pt=1 --shot=C:\tmp\pbr_pt.png
bin\godot.windows.editor.x86_64.console.exe --path misc\pathtracer_test_project -- --view=pbr --pt=0 --shot=C:\tmp\pbr_raster.png
```

All user arguments (after `--`):

| Argument | Default | Meaning |
| --- | --- | --- |
| `--view=` | `overview` | `overview`, `pbr`, `lighting`, `emissive`, `shaders`, `glass`, `fog`, `lights` (48 omni lights; in `--shot`/`--bench` runs of other views those lights are left out) |
| `--pt=` | `1` | `1` path traced, `0` raster |
| `--spp=` | `4` | Path tracer samples per pixel (1-16) |
| `--bounces=` | `3` | Path tracer max bounces (1-8) |
| `--debug=` | `0` | `Environment.pathtracing_debug_mode` (see the enum in `scene/resources/environment.h`) |
| `--denoiser=` | `0` | `0` none, `1` DLSS Ray Reconstruction (needs the Streamline DLLs) |
| `--volfog=` | view default | `1` forces volumetric fog on, `0` off |
| `--frames=` | `90` | Frames to render before the screenshot. Use 300-400 after changing RT shader code: custom hit groups recompile asynchronously and are skipped until ready |
| `--sun_only` | off | Remove every light except the sun (isolates light selection) |
| `--panel_only` | off | Only the emissive room's ceiling panel emits |
| `--linear` | off | Linear tonemap with `--exposure=` (default 0.25), for brightness measurements |
| `--shot=` | none | Save a PNG and quit |
| `--bench=` | none | Print the mean GPU and CPU render time over N frames (after `--frames=` warm-up, default 300), vsync off, and quit |
| `--ser=` | project setting | `1`/`0` turns `rendering/pathtracing/use_shader_execution_reordering` on or off at run time |
| `--adaptive=` | `0` | `1` turns on `Environment.pathtracing_adaptive_sampling` |
| `--adaptive_threshold=` | `0.02` | Adaptive sampling threshold (standard error of tonemapped luminance) |
| `--adaptive_debug=1` | off | Show samples used per pixel instead of the image (blue few, red all) |
| `--restir=` | `0` | `1` turns on `Environment.pathtracing_restir_di` |
| `--orbit=` | off | Turn the camera this many degrees per frame around the view target (tests temporal reuse under motion) |
| `--yaw=` | off | Static camera turned this many degrees around the view target |

## What the path tracer supports today

Audited on 2026-10-08 at commit `ca52415e30`. Shader sources are in
`servers/rendering/renderer_rd/shaders/raytracing/`, the host code in
`servers/rendering/renderer_rd/forward_clustered/render_raytracing.cpp` and
`scene_shader_raytracing.cpp`.

| Feature | Status | Where |
| --- | --- | --- |
| StandardMaterial3D albedo, roughness, metallic, specular, ORM texture | Supported | `scene_raytracing_raygen.glsl`, closest hit HG0 |
| Normal maps | Supported | `apply_normal_map()` |
| Emission (color, energy, texture) | Supported. StandardMaterial3D emitters are also sampled as mesh lights (NEE) on rough surfaces. Custom shader emission is counted on hit only | `lights_sample_emissive_mesh()` |
| Omni, spot, directional lights | Supported, with soft shadows from light size | `raytracing_lights_inc.glsl` |
| Area lights | Not available (Godot has none); emissive meshes are the substitute | |
| Sky | Supported through the radiance octmap | miss shader |
| Depth and height fog | Supported per ray segment | `apply_segment_fog()` |
| Volumetric fog, FogVolume | Supported on primary rays (Godot's froxel fog) | `sample_primary_volumetric_fog()` |
| Custom ShaderMaterial `fragment()` | Supported through custom hit groups | `scene_shader_raytracing.cpp`, `raytracing_custom_fragment_inc.glsl` |
| Custom `vertex()` displacement | Supported when `vertex()` writes `VERTEX` and samples no textures (compute pass, then deformed BLAS) | `raytracing_vertex_displace.glsl`, `process_displaced_surface()` |
| Alpha scissor | Supported (StandardMaterial uses a fixed 0.5 threshold) | any hit |
| Alpha blend (StandardMaterial3D) | Supported: stochastic opacity in any hit; shadows let (1 - alpha) through | `material_alpha_blend_hit()` |
| Refraction (StandardMaterial3D) | Supported: dielectric with Fresnel, IOR = 1 + 10 x `refraction_scale` (0.05 gives 1.5), albedo tints, roughness scatters. Casts opaque shadows (as raster) | `refract_and_bounce()` |
| Custom shader alpha blend, add/sub/mul blending, billboards, proximity fade | Raster overlay on top of the path traced image | `ShaderData::rt_traces_transparency()` |
| Debug views | 22 modes in `Environment.pathtracing_debug_mode` | `debug_visualize()` |
| ReSTIR DI | Opt-in toggle `Environment.pathtracing_restir_di`: temporal reuse of light samples at the primary hit. Best at 1 spp | `raytracing_restir_di_inc.glsl` |
| Adaptive sampling | Opt-in toggle in the Environment's Pathtracing section (`pathtracing_adaptive_sampling`), needs 2+ samples per pixel; samples per pixel becomes the maximum. Disables SER while on | raygen sample loop |

## Steps

Do one step at a time. Each step ends with screenshots compared against raster,
a build, the smoke tests from CUSTOM_BUILD.md, and an update to this file.

| # | Step | Status |
| --- | --- | --- |
| 1 | Audit what the path tracer supports (table above) | Done |
| 2 | Build the test project with all test areas and a screenshot harness | Done |
| 3 | Baseline: screenshots of every view, path traced and raster; list every visible difference in the findings log | Done |
| 4 | Quick bugs: StandardMaterial emission without a texture (B1), StandardMaterial alpha scissor threshold (B2) | Done |
| 5 | Black pixels in directly lit areas (B3): find the cause, fix it | Done |
| 6 | Emission as a light: light sampling toward emissive meshes so emitters light the scene without heavy noise | Done |
| 7 | Custom `vertex()` displacement in the path tracer (B4) | Done |
| 8 | Volumetric fog in the path tracer (B5): Environment volumetric fog first, then FogVolume, then light shafts | Done |
| 9 | Glass (B6): path traced alpha blend, refraction and transmission instead of the raster overlay | Done |
| 10 | Clean up, document, merge `dev` into `nvidia-pt-dlss` | Done (release 0.3.0) |

### Performance roadmap

Second round: make the tracer cheaper per useful sample. Same rules, one
phase at a time on `dev-pt`, each one measured with `--bench` against the
baseline below. For ReSTIR, study [NVIDIA RTXDI](https://github.com/NVIDIA-RTX/RTXDI);
its reservoir logic may be reusable.

| # | Phase | Goal | Impact / difficulty | Status |
| --- | --- | --- | --- | --- |
| 11 | Profile the tracer, verify SER | Baseline numbers | | Done |
| 12 | Adaptive sampling: more rays for noisy pixels, fewer for stable ones | Less wasted work | Scene-dependent / medium | Done |
| 13 | ReSTIR DI: reuse light samples across pixels and frames | Direct light with many lights | High / high | Done (temporal reuse; spatial reuse off, see findings) |
| 14 | ReSTIR GI: reuse indirect paths | Multi-bounce at low spp | High / very high | Next |
| 15 | Path guiding experiments | Better ray directions | Unknown | |

### Baseline (step 11)

2026-10-08, commit after `902466eb50`, RTX 3060 (Ampere), driver 616.92,
1280x720, 4 spp, 3 bounces, no denoiser, mean GPU ms over 150 frames, two runs
each:

| View | Raster | PT, SER on | PT, SER off |
| --- | --- | --- | --- |
| overview | 0.67 | 7.8 | 7.4 |
| pbr | 0.70 | 11.3 | 9.4 |
| lighting | 0.94 | 16.6 | 16.6 |
| emissive | 0.53 | 25.7 | 21.4 |
| shaders | 0.40 | 16.9 | 15.5 |
| glass | 0.64 | 15.9 | 12.4 |
| fog | 0.53 | 7.5 | 6.8 |

Scaling, emissive view, SER off: 1 spp 4.3 ms, 4 spp 21.4, 8 spp 45.8 (about
5.5 ms per extra sample); 1 bounce 6.5 ms, 3 bounces 21.4, 6 bounces 22.3
(most paths end by bounce 3). CPU time stays at 0.2-0.4 ms; the cost is all
GPU. Raster is 0.4-0.9 ms, so the path tracer is 10-50x raster.

## Known bugs

Found in the step 3 baseline (2026-10-08, commit `a6a531899a`, 4 spp, 3
bounces, no denoiser). Screenshots were taken with `--shot` for every view in
both modes.

| ID | View | Bug | Cause / lead |
| --- | --- | --- | --- |
| B1 | emissive | **Fixed in step 4.** StandardMaterial3D emission rendered **black**. Debug mode 21 (Emissive) is 0 on every emitter. Emission from a custom ShaderMaterial works (shaders view, orange stripes). | `scene_raytracing_raygen.glsl` closest hit HG0 only adds emission when `mat.flags & 2` (`RT_MAT_FLAG_HAS_EMISSION_TEX`) is set, which `render_raytracing.cpp` sets only when an emission texture exists. Color and energy alone are ignored. |
| B2 | glass, pbr | **Fixed in step 4.** Alpha scissor sphere with threshold 0.3 and alpha 0.4 is **missing** (no surface, no shadow). | Any-hit HG0 uses a hard-coded `alpha < 0.5`; the material's `alpha_scissor_threshold` isn't passed. |
| B3 | lighting, all | **Fixed in step 5.** Pure black pixels scattered over surfaces that the sun or a lamp lights directly. With light sampling, direct light on a flat diffuse floor should be nearly noise-free. | Unknown. Some paths return zero radiance. Debug mode 22 (BRDF rejection) shows rejection noise on every surface. Check NEE shadow rays, `offset_ray_origin`, and BRDF sample rejection. |
| B4 | shaders | **Fixed in step 7** (no textures in `vertex()` yet). `vertex()` displacement was ignored: the wave renders flat and casts a flat shadow. | The BLAS is built from the original mesh. Needs the vertex shader applied before the BLAS build (a compute pass or the raster pipeline's transform feedback). |
| B5 | fog | **Fixed in step 8.** Volumetric fog, FogVolume and the spot light shaft were **not rendered at all**. Only distance/height fog worked. | The path tracer did not update or sample Godot's integrated volumetric fog froxel map. |
| B6 | glass | **Fixed in step 9** (StandardMaterial3D only). Alpha blend and refraction materials were drawn by the raster transparent pass on top of the path traced image. No shadows or reflections, and refraction smears the noisy screen texture into horizontal streaks. | Transparent geometry is skipped by the path tracer (`transmissivness = 0.0`). |

Works as expected (path traced is equal to or better than raster): the PBR
sphere grid (reflections of the real scene instead of a darker sky probe),
normal and albedo textures, the Cornell box (mirror sphere, color bleeding, soft
shadows from omni and spot), custom shader albedo, roughness, emission, `TIME`,
`NORMAL_MAP` and `ALPHA_SCISSOR_THRESHOLD`.

Other notes:
- Noise at 4 spp without a denoiser is expected; use `--denoiser=1` with the
  Streamline DLLs for the final look.
- Glass needs more bounces than opaque scenes: each interface uses one. At
  the default 3, some paths inside a glass sphere end black.

## Findings log

Newest first. Note the date, the commit, the view and what you saw or changed.

- 2026-10-08: Step 13 follow-up: ReSTIR spatial reuse investigated. Test:
  lights view, 1 spp, direct light only, linear, temporary shader switches.
  Findings, in order:
  - With the light order fixed (sorted by RID), spatial reuse is unbiased
    with a static camera (0.0188 vs 0.0190) and with a moving one (0.0181 vs
    0.0181). With the old order (sorted by a camera-dependent score) it blew
    up under motion. A static camera with the order rotated by one every
    frame blew up the same way (x4.8 after 120 frames), so the trigger is
    light indices changing between frames, not motion.
  - Temporal reuse alone stays unbiased with a rotating order (0.0190), and
    goes 65% dark without the index remap, so the remap is applied and works.
  - Spatial reuse with radius 0 (three more reads of the pixel's own slot)
    is unbiased with a rotating order; radius 24 is not. Reading other
    pixels' reservoirs is what breaks.
  - Not the cause (each checked on the GPU or by switching it off): stale
    reservoir data (frame stamps were all last frame's), the remap (the
    remapped light's position matched the stored one), cache coherence
    (`coherent` images: identical output), the balance heuristic's weight
    functions (one shared function: same result), the history cap (M <= 2:
    same), only remapped lights (reusing only lights whose index didn't
    change: still biased), the light buffer being a frame late (stamped:
    always current), several updates per frame (one per engine frame).
  - Root cause still unknown. Fix applied: `gather_lights()` keeps the best
    lights by score but orders them by RID, and emissive meshes are ordered
    by surface, so indices only change when a light enters or leaves the
    set. That stops the blow-up; spatial plus temporal reuse under motion is
    then 9% dark (0.0146 vs 0.0160) and no less noisy than temporal alone
    (RMSE 0.064 vs 0.062), so spatial reuse stays off. Temporal-only results
    are unchanged (0.0190 static, 0.0157 moving, emissive 0.0311 = ref).

- 2026-10-08: Step 13 done (ReSTIR DI, temporal reuse). New Environment
  toggle `pathtracing_restir_di` (C++-only server call, like adaptive
  sampling), raygen variant `USE_RESTIR_DI` (`RT_FLAG_RESTIR_DI`), new test
  view `lights` (48 small omni lights between pillars) and harness options
  `--restir`, `--orbit`, `--yaw`.
  Design: a sample is (light, random numbers for the point on it), so any
  pixel re-evaluates it with `lights_eval_light()` /
  `lights_eval_emissive_mesh()` (the NEE code was split into pick, evaluate
  and one shared shadow ray). At the primary hit, sample 0 resamples its RIS
  candidate with last frame's reservoir at the reprojected pixel, weighting
  with the generalized balance heuristic (other surfaces judged by
  `lights_selection_weight()`), traces one shadow ray and stores the
  reservoir (rgba32ui sample + rgba32f surface images, ping-pong by frame
  parity, bindings 34-38). Light indices change every frame (lights are
  re-sorted by a camera-dependent score), so the host uploads a previous to
  current index map built from light instance RIDs and emissive surface
  pointers. Samples 1+ of a pixel hit the same surface (no jitter) and reuse
  sample 0's reservoir (`shadercallcoherent` images; W's sign records the
  shadow, so no extra shadow ray).
  Measured, lights view, 1 spp: direct light RMSE 0.110 -> 0.058, full path
  (linear) 0.115 -> 0.073, tonemapped 0.356 -> 0.255; linear mean equal to
  the 64 spp reference (0.0190), also with a moving camera (0.0157 vs
  0.0160, RMSE 0.103 -> 0.062). Emissive view (mesh lights): unbiased, RMSE
  0.111 -> 0.089. Cost: 1 spp 6.09 -> 6.12 ms, 4 spp 24.8 -> 29.6 ms. At 4
  spp the samples share one direct light estimate, so ReSTIR helps little
  there; it's meant for 1 spp with a denoiser.
  Dead ends, all measured: (1) plain 1/M weights with visibility reuse: 28%
  too dark. (2) M counted only over surfaces the sample can reach: unbiased
  per frame, but rare large weights fed on themselves through reuse
  (fireflies spreading, 30% too bright in some runs). (3) Storing 1/M weights
  to stop that: stable but 17% dark. The balance heuristic fixed all three.
  (4) Running the full resampling for every sample: 3x slower, because
  lanes drift apart across samples and the primary-hit code and its shadow
  ray then run in turns with the later bounces' NEE; one shared pick and
  shadow-ray call site cut it, reusing sample 0's result fixed it.
  (5) Spatial reuse from last frame's neighbors: fine with a static camera,
  but under motion the image brightened frame after frame (x5.7 after 120
  frames at 0.1 degrees per frame), and it added almost nothing over
  temporal reuse here (direct RMSE 0.057 vs 0.058). Not caused by the light
  remap, the neighbor weight functions or the reprojection (each tested).
  Left off (`RESTIR_SPATIAL_SAMPLES 0`); open for a later step.
  Also found: `vec3_to_oct()` returns [0, 1] but `oct_to_vec3()` takes
  [-1, 1]; decoding without remapping broke every normal check. And the
  NEE refactor alone skips shadow rays toward lights that can't contribute:
  emissive view 21.4 -> 17.6 ms, lights view 31.1 -> 25.0 ms with ReSTIR off.
  Seen once, not reproduced in 20+ runs: `Attempted to free invalid ID` in
  the first run after a rebuild.

- 2026-10-08: Step 12 follow-up: the on/off switch moved from the project
  setting to `Environment.pathtracing_adaptive_sampling` (Pathtracing section
  of the Environment inspector; the tooltip says it needs 2+ samples per
  pixel). New C++-only server call
  `environment_set_pathtracing_adaptive_sampling()`; the bound
  `environment_set_pathtracing()` is unchanged. Threshold and debug stay
  project settings. Verified: overview 16 spp 30.9 -> 18.3 ms with the toggle,
  all views clean, smoke tests pass.
- 2026-10-08: Step 12 done (adaptive sampling). New project settings
  `rendering/pathtracing/adaptive_sampling` (off by default),
  `adaptive_sampling_threshold` (0.02) and `adaptive_sampling_debug`; raygen
  variant `USE_ADAPTIVE_SAMPLING` (`RT_FLAG_ADAPTIVE_SAMPLING`, params 9-10).
  The raygen sample loop keeps a Welford variance of the tonemapped luminance
  L/(1+L) and stops once the squared standard error is below threshold^2,
  after at least max(2, spp/4) samples, and only when `subgroupAll()` agrees:
  a warp runs until its slowest pixel is done anyway, so converged pixels keep
  sampling for free and time is saved only where whole warps converge. Needs
  subgroup vote in raygen (checked via `LIMIT_SUBGROUP_IN_SHADERS`).
  Measured (GPU ms, 16 spp max): overview 30.5 -> 18.5, pbr 38.8 -> 26.0,
  glass 48.2 -> 32.8, emissive 91.3 -> 87.7 (noisy everywhere, little to
  save). At 4 spp it saves 0-20%. Equal-time quality against a 64 spp
  reference: overview RMSE 0.0181 (fixed 10 spp, 19.2 ms) vs 0.0148 (adaptive,
  18.5 ms), glass 0.0276 vs 0.0223; means match, no visible bias or block
  artifacts. Rejected: a minimum of 2 samples (pixels whose two samples agree
  by chance stop, dark specks); stopping each pixel on its own (RMSE worse
  than fixed spp and biased dark, mean 0.565 vs 0.570); `subgroupAll()` after
  SER reordering (driver crash, 0xC0000005). So adaptive sampling turns SER
  off while enabled. Not made the default yet: it changes how many samples a
  scene gets.

- 2026-10-08: Step 11 done (profile, SER). Added `--bench` and `--ser` to the
  test harness; numbers above. Finding: SER was turned on by the project
  setting alone (default on), without checking the GPU. The RTX 3060 exposes
  `VK_EXT_ray_tracing_invocation_reorder` but reports reordering hint `NONE`
  (Ampere accepts the calls and doesn't reorder), so SER only added 0-28%
  GPU time (emissive +20%, glass +28%). Fix: new internal feature
  `RD::SUPPORTS_RAYTRACING_INVOCATION_REORDER` (Vulkan: extension enabled and
  hint `REORDER`); the path tracer uses SER only when it's true. The verbose
  log prints `invocation reorder (SER): ...`. Verified: with the setting on,
  the 3060 now runs at the SER-off times; all seven views render the same on
  the non-SER path, with no `ERROR:` or `WARNING:`. On Ada and newer SER stays
  on; not measured here (no such GPU).

- 2026-10-08: Step 9 done. Alpha blended and refractive StandardMaterial3D
  surfaces are path traced instead of drawn by the raster overlay.
  `ShaderData::rt_traces_transparency()` picks them (BaseMaterial3D code,
  mix blending, depth test on, no billboard, proximity fade or stencil) and
  gives them opaque `rt_pass_flags`, so they enter the TLAS. Material flags:
  bit 3 alpha blend, bit 4 refraction, bits 24-31 the IOR. Alpha blend: the
  HG0 any hit (and `ray_query_alpha_test()`) keeps the hit with probability
  alpha, from a hash of the ray's random state and the triangle, so repeated
  any-hit calls agree and a pass-through costs no bounce. Shadow rays get the
  same test, seeded per light sample. Refraction: the closest hit shades the
  alpha part as usual and sends the rest to `refract_and_bounce()` (exact
  dielectric Fresnel, GGX microfacet for rough glass, albedo tint, total
  internal reflection). Refractive instances disable face culling, so rays
  leave through back faces. Verified: glass view shows the alpha sphere with a
  partial shadow and the refraction sphere with an inverted, bent view of the
  pillars. At 3 bounces some paths inside the glass end black; 6 bounces
  clean that up. All views render with no `ERROR:` or `WARNING:`.

- 2026-10-08: Fixed the dark specks on the emissive room's roof (step 7
  follow-up). Cause: `lights_mesh_selection_weight()` bounded each emitter
  with a sphere. The wide ceiling panel's sphere pokes through the roof
  plane, so roof points picked the panel and got a blocked shadow ray. Now
  `RT_EmissiveMeshData` also carries the world AABB half extents (96 bytes),
  and the weight is 0 when every box corner is behind the shading plane.
  That test is exact, so no bias. Verified: overview roof is clean, emissive
  view unchanged. All seven views render with no `ERROR:` or `WARNING:` in
  both modes.
- 2026-10-08: Follow-up for step 8: fixed volumetric fog `Sky Affect` in the
  path tracer. Cause: raster applies `Environment.volumetric_fog_sky_affect`
  in `sky.glsl`, but the PT miss shader composited the froxel fog over sky at
  full strength. RT params now carry `volumetric_fog_sky_affect` and the global
  fog legacy-blending flag; miss shader uses the same sky-compose formulas as
  raster. Test harness accepts `--vfog_sky_affect=`. Verified PT screenshots at
  `--vfog_sky_affect=0` and `1` (`fog_pt_sky_affect_0.png`,
  `fog_pt_sky_affect_1.png`) and raster counterparts show matching direction:
  sky unaffected at 0, fully affected at 1. Build passed.
- 2026-10-08: Follow-up for step 8: fixed the `4 RIDs of type "Shader" were
  leaked` warning on PT exit. Cause: async RT pipeline build tasks can finish
  during shutdown before `drain_completed_compiles()` installs them; the shutdown
  path freed the task's new pipeline but not task-owned per-hit-group shader
  RIDs. Added `_free_task_owned_outputs()` and use it for abandoned current /
  queued tasks. Verified `--view=fog --pt=1 --frames=180` exits with no shader
  RID leak warning.
- 2026-10-08: Step 8 done. Path traced fog view now renders volumetric fog,
  FogVolume and the spot light shaft. Forward+ path tracing now performs the
  normal voxel GI setup before volumetric fog so `rbgi->voxel_gi_textures[]`
  contains default textures even when the scene has no VoxelGI; without this,
  the volumetric fog process uniform set failed at binding 13. It updates
  `RB_SCOPE_FOG` before TLAS/uniform setup, sets `RT_FLAG_FOG_ENABLED` when a
  volumetric fog map exists, binds `VolumetricFog::fog_map` at RT binding 29,
  and passes inverse fog length/detail-spread plus a has-volumetric-fog flag in
  RT params 4-6. The RT shader samples the integrated froxel map on primary
  hits/misses and composites it as raster does (`rgb` in-scatter, `a`
  transmittance). Secondary rays keep the existing per-segment distance/height
  fog instead of sampling the camera-space froxel map. Verified with
  `--view=fog --pt=1 --frames=180` against raster (`fog_pt_b5_final.png`,
  `fog_raster_b5_fix.png`). Build passed. Smoke passed: PhysX GPU normal
  and editor runs printed `PhysX 5.10.0 initialized [GPU]` with no `ERROR:`;
  path traced fog screenshot exited cleanly with no `ERROR:`.
- 2026-10-08: Step 7 done. Custom `vertex()` displacement now shows in the
  path tracer. `_preprocess_shader()` marks shaders that write `VERTEX`
  (`write_flag_pointers`). `raytracing_vertex_displace.glsl` is a ShaderRD
  compute template whose expanded source gets the material's vertex code,
  uniform members and vertex-stage globals at runtime
  (`SceneShaderRaytracing::get_vertex_displace_pipeline()`, one program per hit
  group slot). `process_displaced_surface()` (TLAS loop, before
  `process_surface()`) fills the static `GeometryData`, dispatches the compute
  once per frame into a per-surface uncompressed vertex buffer, then hands it
  to `process_deformed_surface()` (BLAS refit, motion vectors). The geometry
  gets `FLAG_VERTEX_DISPLACED`, and the hit template restores
  `vertex`/`normal`/`tangent`/`binormal` after re-running `vertex()` so the
  hit isn't displaced twice. Limits: `vertex()` that samples a texture fails
  to compile in the pass (warning, traced undisplaced); 2D meshes skipped.
  Also fixed: exit errors `Attempted to free invalid ID` (a BLAS is freed with
  the mesh buffers it was built from; added
  `RenderingDevice::acceleration_structure_is_valid()` and use it in the
  cache cleanup) and the old `4 RIDs of type "Shader" were leaked` warning no
  longer shows. Verified: wave and its shadow match raster.
  Follow-up (fixed later): the emissive room's roof showed dark specks,
  because light selection didn't know the emitters inside are hidden from
  the roof.
- 2026-10-08: Step 6 done. Emissive StandardMaterial3D surfaces are mesh
  lights now. Host (`render_raytracing.cpp`, TLAS loop): each emissive HG0
  surface becomes an `RT_EmissiveMeshData` (transform incl. compression AABB,
  bounds, geometry index, triangle count, power), uploaded at binding 33, count
  in `RT_PARAM_EMISSIVE_MESH_COUNT` (13), up to 256; its geometry gets
  `FLAG_EMISSIVE_LIGHT`. Shader: mesh lights join the RIS candidates
  (`lights_mesh_selection_weight()`), sampling picks a uniform triangle and a
  uniform point (`lights_sample_emissive_mesh()`), emission read from the
  material incl. texture. Double counting is avoided without MIS: at a vertex
  with roughness >= 0.3 NEE samples mesh lights and sets payload bit 27
  (`EMISSIVE_SAMPLED_FLAG`); the next hit then skips emission from flagged
  geometry. Glossy vertices leave emitters to the BRDF ray (sharp reflections).
  Verified unbiased: `--panel_only --sun_only --linear --spp=16`, mean linear
  brightness vs a build with mesh NEE disabled matches within 1-2% in the room
  and 0.1% on the sunlit floor (`png_mean`-style sRGB-decoded means; compare
  in linear space, tonemapped means are skewed by noise and clipping).
- 2026-10-08: Step 5 done. B3 cause: `lights_evaluate_direct_lighting()`
  picked one light per hit **uniformly** among the lights in range, so a
  sunlit pixel sampled the sun only 1/3 to 1/4 of the time (the scene has a
  sun, an omni and two spots) and ~30% of pixels at 4 spp never saw it.
  Confirmed with `--sun_only` (sunlit floor became clean). Fix: resampled
  importance sampling with `lights_selection_weight()` (emission luminance x
  attenuation x conservative cosine, spot cone check with a small floor); with
  at most `RT_LIGHT_RESERVOIR_SIZE` (16) lights every light is a candidate.
  Weights are zero only where a light can't contribute, so it stays unbiased
  (brightness matches the old images). Remaining grain is indirect light.
- 2026-10-08: Step 4 done. B1: the closest hit now computes emission from
  color times energy and only multiplies by the texture when one is set. The
  host reads `emission`, `emission_energy` and `texture_emission` only when the
  material's shader declares `emission` (BaseMaterial3D stores every parameter
  but declares the uniform only with emission enabled), so a disabled emission
  never glows. B2: the host packs `alpha_scissor_threshold` into bits 16-23 of
  `MaterialData.flags` (`RT_MAT_ALPHA_THRESHOLD_SHIFT`, read in GLSL with
  `material_alpha_threshold()`); any hit and the ray query shadow test use it.
  Materials without alpha scissor keep the old 0.5 cutoff. Verified: emitters
  glow and light the emissive room (noisy, step 6), the 0.3 scissor sphere and
  its shadow appear. Smoke tests pass.
- 2026-10-08: Step 3 done. Six bugs found (table above). B1 has a confirmed
  root cause. Next is step 4 (B1 and B2).
- 2026-10-08: Steps 1 and 2 done. No fixes yet.
