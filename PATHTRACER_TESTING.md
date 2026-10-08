# Path tracer test and fix plan

Working notes for making the NVIDIA path tracer (`RenderForwardClusteredPT`)
handle PBR materials, lights, emission, custom shaders and fog natively. This
file is the shared memory for humans and agents working on it: read it before
starting, and update the step table and the findings log as you go.

Work happens on the `dev` branch (see [CUSTOM_BUILD.md](CUSTOM_BUILD.md)).

## How to run the test scene

The test project is [`misc/pathtracer_test_project/`](misc/pathtracer_test_project/).
It builds every test area from code (`main.gd`), so there is no scene file to
keep in sync. The path tracer needs Forward+ and the Vulkan driver; the
project sets both.

Interactive:

```
bin\godot.windows.editor.x86_64.exe --path misc\pathtracer_test_project
```

Keys: `1`-`7` switch views, `P` toggles path tracing, `D` cycles the debug
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
| `--view=` | `overview` | `overview`, `pbr`, `lighting`, `emissive`, `shaders`, `glass`, `fog` |
| `--pt=` | `1` | `1` path traced, `0` raster |
| `--spp=` | `4` | Path tracer samples per pixel (1-16) |
| `--bounces=` | `3` | Path tracer max bounces (1-8) |
| `--debug=` | `0` | `Environment.pathtracing_debug_mode` (see the enum in `scene/resources/environment.h`) |
| `--denoiser=` | `0` | `0` none, `1` DLSS Ray Reconstruction (needs the Streamline DLLs) |
| `--volfog=` | view default | `1` forces volumetric fog on, `0` off |
| `--frames=` | `90` | Frames to render before the screenshot |
| `--sun_only` | off | Remove every light except the sun (isolates light selection) |
| `--panel_only` | off | Only the emissive room's ceiling panel emits |
| `--linear` | off | Linear tonemap with `--exposure=` (default 0.25), for brightness measurements |
| `--shot=` | none | Save a PNG and quit |

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
| Volumetric fog, FogVolume | **Not supported** | no reference in the RT code |
| Custom ShaderMaterial `fragment()` | Supported through custom hit groups | `scene_shader_raytracing.cpp`, `raytracing_custom_fragment_inc.glsl` |
| Custom `vertex()` displacement | Unknown, to test | |
| Alpha scissor | Supported (StandardMaterial uses a fixed 0.5 threshold) | any hit |
| Alpha blend, refraction, transmission | **Not supported** (`transmissivness = 0.0`) | `shade_and_bounce()` |
| Debug views | 22 modes in `Environment.pathtracing_debug_mode` | `debug_visualize()` |

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
| 7 | Custom `vertex()` displacement in the path tracer (B4) | Next |
| 8 | Volumetric fog in the path tracer (B5): Environment volumetric fog first, then FogVolume, then light shafts | |
| 9 | Glass (B6): path traced alpha blend, refraction and transmission instead of the raster overlay | |
| 10 | Clean up, document, merge `dev` into `nvidia-pt-dlss` | |

## Known bugs

Found in the step 3 baseline (2026-10-08, commit `a6a531899a`, 4 spp, 3
bounces, no denoiser). Screenshots were taken with `--shot` for every view in
both modes.

| ID | View | Bug | Cause / lead |
| --- | --- | --- | --- |
| B1 | emissive | **Fixed in step 4.** StandardMaterial3D emission rendered **black**. Debug mode 21 (Emissive) is 0 on every emitter. Emission from a custom ShaderMaterial works (shaders view, orange stripes). | `scene_raytracing_raygen.glsl` closest hit HG0 only adds emission when `mat.flags & 2` (`RT_MAT_FLAG_HAS_EMISSION_TEX`) is set, which `render_raytracing.cpp` sets only when an emission texture exists. Color and energy alone are ignored. |
| B2 | glass, pbr | **Fixed in step 4.** Alpha scissor sphere with threshold 0.3 and alpha 0.4 is **missing** (no surface, no shadow). | Any-hit HG0 uses a hard-coded `alpha < 0.5`; the material's `alpha_scissor_threshold` isn't passed. |
| B3 | lighting, all | **Fixed in step 5.** Pure black pixels scattered over surfaces that the sun or a lamp lights directly. With light sampling, direct light on a flat diffuse floor should be nearly noise-free. | Unknown. Some paths return zero radiance. Debug mode 22 (BRDF rejection) shows rejection noise on every surface. Check NEE shadow rays, `offset_ray_origin`, and BRDF sample rejection. |
| B4 | shaders | `vertex()` displacement is ignored: the wave renders flat and casts a flat shadow. | The BLAS is built from the original mesh. Needs the vertex shader applied before the BLAS build (a compute pass or the raster pipeline's transform feedback). |
| B5 | fog | Volumetric fog, FogVolume and the spot light shaft are **not rendered at all**. Only distance/height fog works. | Not implemented. Needs ray marching through Godot's froxel fog volume, or a path traced participating medium. |
| B6 | glass | Alpha blend and refraction materials are drawn by the raster transparent pass on top of the path traced image. No shadows or reflections, and refraction smears the noisy screen texture into horizontal streaks. | Transparent geometry is skipped by the path tracer (`transmissivness = 0.0`). |

Works as expected (path traced is equal to or better than raster): the PBR
sphere grid (reflections of the real scene instead of a darker sky probe),
normal and albedo textures, the Cornell box (mirror sphere, color bleeding, soft
shadows from omni and spot), custom shader albedo, roughness, emission, `TIME`,
`NORMAL_MAP` and `ALPHA_SCISSOR_THRESHOLD`.

Other notes:
- Noise at 4 spp without a denoiser is expected; use `--denoiser=1` with the
  Streamline DLLs for the final look.
- Exit prints `WARNING: 4 RIDs of type "Shader" were leaked` with path tracing
  on. Probably the custom hit group shaders. Low priority.

## Findings log

Newest first. Note the date, the commit, the view and what you saw or changed.

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
