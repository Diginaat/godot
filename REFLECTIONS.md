# Reflections: probes and planar reflections with DDGI

Working notes and documentation for the reflection system of this build:
budgeted `ReflectionProbe` updates, DDGI-normalized probe reflections,
on-demand reflection atlas memory, and planar reflections
(`PlanarReflector3D`). Read it before changing any of them, and update the
step table and the findings log as you go. Work happens on the
`dev-ddgi-reflections` branch.

Both systems are GPU-vendor independent (rasterization and compute only, no
ray tracing, no CUDA/OptiX) and work without DDGI. With DDGI, probe
reflections follow DDGI's lighting.

## What it does

| Part | What | Renderers |
| --- | --- | --- |
| Probe update budget | `ReflectionProbe`s are captured face by face, at most N cubemap faces per frame for all probes together; a probe is shown when all six faces are done | All |
| Probe priority | Probes with no reflection yet, moved or changed first; realtime probes by age over distance | All |
| Probe toggle | Reflection probes off entirely (no updates, no shading) | All |
| Atlas on demand | The reflection atlas texture starts with 4 layers and doubles when probes need them (up to `reflection_count`), copying the captured layers | Forward+, Mobile |
| DDGI normalization | Probe reflections scaled by DDGI irradiance at the pixel over the probe's own irradiance | Forward+ with DDGI |
| DDGI over probe ambient | Where DDGI covers a pixel, it keeps DDGI's diffuse light instead of the probe's blurred cubemap | Forward+ with DDGI |
| Planar reflections | `PlanarReflector3D`: exact mirror image of the screen across a rectangle, by pixel projection; no second scene render | Forward+ |

## How to use it

Reflection probes: use `ReflectionProbe` nodes as usual. "Once" probes are
captured over a few frames when they appear or move, then never again;
with DDGI their reflections still follow light changes (normalization).
"Always" probes refresh within the face budget.

Planar reflections: add a `PlanarReflector3D` (Add Node >
`PlanarReflector3D`) on a flat reflective surface. Its X and Z axes span
the rectangle (`size`), its Y axis points to the reflected side (rotate it
90 degrees about X for a wall mirror). Cover the surface; the material
decides how reflective it is (metallic, roughness, specular).

| `PlanarReflector3D` property | Default | Meaning |
| --- | --- | --- |
| `size` | 10 x 10 m | Rectangle along X and Z |
| `intensity` | 1.0 | How much the planar reflection replaces probes, SSR and sky on it |
| `roughness_cutoff` | 0.4 | Rougher surfaces keep the probe/sky reflection (fades out above 60% of it) |
| `normal_distortion` | 0.0 | How much normal maps and waves bend the reflection (water: 0.2 to 0.5) |
| `thickness` | 0.05 m | How far from the plane a surface still counts as the reflector (waves); objects nearer than this aren't reflected |
| `layers` | 1 | Render layers; cameras whose cull mask shares one use it |

Mirrors that face the camera reflect what is behind it, which is off screen:
put a box-projected `ReflectionProbe` in the room for that part. Water: a
transparent material works (the reflector is matched by depth, not by the
depth buffer).

### Automatic reflections (DDGIVolume)

With a `DDGIVolume`, reflections need no setup: `auto_reflections` (on by
default) creates them in the RenderingServer, nothing is added to or saved
with the scene.

| `DDGIVolume` property | Default | Meaning |
| --- | --- | --- |
| `auto_reflections` | on | Master switch |
| `auto_reflection_probe` | on | One box-projected reflection probe for the volume (ambient off: DDGI gives the diffuse light), captured once; with `infinite_world` it follows the camera in steps of a quarter of the volume. Skipped when the volume has a `ReflectionProbe` of its own |
| `auto_planar_reflections` | on | Planar reflectors on large flat surfaces of `MeshInstance3D`s whose material roughness (`BaseMaterial3D.roughness`, or a `ShaderMaterial` `roughness` parameter) is at most `auto_planar_max_roughness`; coplanar pieces merged, undersides skipped, the 16 largest kept, surfaces with a `PlanarReflector3D` left alone |
| `auto_planar_max_roughness` | 0.3 | Smoothest-surfaces-only by default. Raise it (up to 1) for floors that turn glossy at runtime, such as wet floors from a shader: the reflection then appears where the per-pixel roughness drops below the reflector's cutoff, at the planar pass cost while they are in view |
| `auto_planar_min_area` | 2 m² | Smaller flat surfaces are ignored |

Scanning: on start and after meshes, probes or reflectors are added or
removed (at most every 0.5 s), every 3 s for material and mesh changes, and
when a reflecting mesh moves (checked every 0.2 s). Plane groups are cached
per mesh; the server objects only change when the result does. Measured
(reflect view, 1080p, RTX 3060): automatic 3.104 ms GPU / 0.522 ms CPU,
the same scene with hand-placed reflectors 3.103 / 0.521 ms.

Project settings (`rendering/global_illumination/reflections/`):

| Setting | Default | Meaning |
| --- | --- | --- |
| `probes_enabled` | on | Reflection probes on/off |
| `probe_update_faces_per_frame` | 2 | Cubemap faces per frame for all probes; 0 = previous behavior (Once: 6 faces in one frame; Always: every frame) |
| `probe_ddgi_normalization` | 1.0 | Strength of the DDGI normalization (0 = off) |
| `probe_normalization_max` | 4.0 | Largest brightening factor of the normalization |
| `ddgi_overrides_probe_ambient` | on | DDGI's diffuse light wins over the probe ambient where DDGI covers the pixel |
| `planar_enabled` | on | Planar reflections on/off |
| `planar_max_reflectors` | 4 | Most reflectors per view (nearest first, 1 to 8) |
| `planar_resolution` | Full | Full or Half |

Probe resolution and count stay in the existing
`rendering/reflections/reflection_atlas/reflection_size` and
`reflection_count` (count is now a maximum, not an allocation).

Quality per hardware (suggestions, from the measurements below):

| Hardware | Suggested |
| --- | --- |
| Weak / integrated GPUs | `planar_resolution` Half, `probe_update_faces_per_frame` 1, "Once" probes, `reflection_size` 128 |
| Mid-range | Defaults |
| High-end | `probe_update_faces_per_frame` 6 for realtime probes that must react fast |

## Design decisions

Chosen after comparing the published techniques against this renderer
(sources at the end).

**Planar reflections: pixel-projected reflections (PPR), not a second
render.** The common alternative, rendering the scene again from a mirrored
camera (Unreal's Planar Reflection, Unity HDRP's Planar Reflection Probe),
is exact and sees off-screen objects, but it costs a full scene render per
reflector: triangle and draw call cost don't scale with its resolution
(Unreal's own documentation says so), and shadows, GI and post-processing
must be handled again. Ray marched SSR is generic but noisy, costly at full
resolution and misses thin features. PPR (Cichocki, SIGGRAPH 2017) inverts
the reflection: each pixel is mirrored across the plane and scattered to
where it is seen, so a planar reflection needs no search. It is exact for
everything on screen, deterministic (no noise, no temporal filter), costs
two compute passes independent of scene complexity and runs on any GPU with
32-bit image atomics. Its limit is the same as SSR's: off-screen content is
missing, so it falls back to probes and the sky. Reflections are composited
into the PBR pipeline exactly like Godot's SSR: they replace the incoming
specular light before the BRDF's split-sum term, so Fresnel, roughness and
metallic apply and nothing is added twice.

| Planar option | Quality | GPU cost (1080p) | Memory | Off-screen | Complexity |
| --- | --- | --- | --- | --- | --- |
| Mirrored camera re-render | Exact | A scene render per reflector (CPU and triangle cost) | A render target per reflector | Yes | High (culling, shadows, GI, post) |
| SSR (ray marched) | Approximate, noisy | Already in Godot; depends on steps | Small | No | Exists |
| PPR (chosen) | Exact on screen | 0.34 ms full, 0.10 ms half (RTX 3060) | 28 MB full, 7 MB half | No (falls back) | Medium |
| Hardware ray tracing | Exact | High | BVH | Yes | Not vendor independent enough (needs RT hardware) |

**Reflection probes: build on Godot's `ReflectionProbe`, don't duplicate
it.** Godot already has box projection, blending, an octahedral atlas,
GGX filtering and clustered lookup. What it lacked: (1) "Always" probes
render six faces and filter every frame (3.2 ms for one probe here), "Once"
probes render six faces in one frame (a spike); (2) captured probes don't
follow light changes; (3) the atlas allocates all 64 layers up front (512
MB at the default settings, even for one probe); (4) inside a probe, its
blurred cubemap replaced DDGI's diffuse light.

- Time slicing as in Unity's "Individual Faces" mode, but with one shared
  budget for all probes and a priority. The faces go to the atlas' shared
  staging cubemap and are converted to the probe's octahedral map only
  after the sixth, so a reflection never mixes old and new faces.
- Light changes: **normalization** instead of relighting. Relit G-buffer
  probes (Assassin's Creed 4, Ghost of Yotei) follow every light but need a
  G-buffer cubemap per probe and a light loop over it; normalization (Call
  of Duty: Black Ops II, Frostbite, Unity 6 APV) is one texture read per
  probe and pixel, and DDGI already provides the irradiance it needs at
  every pixel. Factor = luminance(DDGI irradiance at the pixel) /
  luminance(probe's roughest level in the normal direction), clamped to
  [0, `probe_normalization_max`], blended by the strength and by DDGI's
  coverage of the pixel. It also darkens probe reflections in corners and
  under objects the probe saw lit (a leak the DDGI sky occlusion didn't
  cover, since probes replace the sky).
- Memory: the atlas texture grows by doubling, copying captured layers
  (`RenderingDevice.texture_copy`), so growth never recaptures. An LRU bug
  was fixed on the way (the eviction search started at 0 and never found
  a slot).

## Architecture

| Part | Where |
| --- | --- |
| Settings | `servers/rendering/rendering_server.cpp` (`rendering/global_illumination/reflections/*`) |
| Probe budget, priority, toggle | `servers/rendering/renderer_scene_cull.*` (`render_probes()`, `_render_reflection_probes_budgeted()`, `_render_reflection_probe_face()`, `CullData::reflection_probes_enabled`) |
| Atlas on demand, LRU fix | `servers/rendering/renderer_rd/storage_rd/light_storage.*` (`_reflection_atlas_grow()`, `reflection_probe_instance_begin_render()`) |
| Normalization, DDGI ambient | `shaders/scene_forward_lights_inc.glsl` (`reflection_process()`), `shaders/forward_clustered/scene_forward_clustered.glsl` (GI and reflection blocks) |
| Planar reflector API | `RenderingServer.planar_reflection_*` (`rendering_server.h`, `rendering_server_default.h`, `rendering_method.h`); registry per scenario in `renderer_scene_cull.*`; passed to `render_scene()` as `RenderPlanarReflection` (`renderer_scene_render.h`) |
| Planar node and gizmo | `scene/3d/planar_reflector_3d.*`, `editor/scene/3d/gizmos/planar_reflector_3d_gizmo_plugin.*`, `editor/icons/PlanarReflector3D.svg` |
| Planar passes | `renderer_rd/effects/planar_reflections.*`, `shaders/effects/planar_reflections.glsl` |
| Planar in the frame | `render_forward_clustered.*` (`_process_planar_reflections()`, bindings 40/41, `SCREEN_SPACE_EFFECTS_FLAGS_USE_PLANAR_REFLECTIONS`), `effects/ss_effects.*` (last frame buffer) |

Per frame, for planar reflections (Forward+, main view):

1. **Select** (CPU): the scenario's visible reflectors on the camera's
   layers, facing the camera, in the frustum; the nearest
   `planar_max_reflectors`. None: no pass, and the buffers are freed.
2. **Depth pre-pass** with normals and roughness (forced while planar
   reflections are used).
3. **Project** (compute, one thread per pixel): each pixel above a
   reflector's plane (by more than its thickness) is mirrored, the camera
   ray to the mirrored point meets the rectangle at the reflection point,
   which must not be behind the depth buffer nor covered by a nearer
   reflector. The screen offset from the pixel to there is packed into 32
   bits (component along the dominant axis in the high bits, so smaller
   values mean nearer geometry) and written with an atomic max of the
   inverted value into an R32UI buffer.
4. **Resolve** (compute, one thread per pixel): finds the reflector the
   camera ray meets in front of the depth buffer; reads the offset, fills
   one-pixel holes from the 4 neighbors (a neighbor's offset if there is
   none or if it is clearly shorter), fetches the last frame's color at the
   reflected point (reprojected, blurred by roughness x distance through
   its mipmaps), limits bilinear color bleeding (luma/chroma toward the
   point sample) and fades near the screen edges. Writes color
   (premultiplied, RGBA16F) and params (plane depth, thickness, distortion,
   roughness cutoff; RGBA16F).
5. **Shade**: opaque and transparent fragments whose view depth matches
   the plane depth use it, after SSR: `specular = specular * (1 - a) + rgb`,
   with the lookup shifted by the normal map.
6. After the transparent pass, the frame is copied to the last frame
   buffer (full size, 6 mipmaps; shared with SSR and SSIL).

## Measurements

RTX 3060, Vulkan, 1920x1080 native, `reflect` test view (animated: moving
light and ball), DDGI Medium, `--gpu-profile`, 150 warm-up + 300 measured
frames, 2026-10-31. `gpu` is the whole frame's GPU time.

| Run | gpu ms | Reflection passes ms | VRAM MB |
| --- | --- | --- | --- |
| Probes + planar (defaults), atlas on demand | 3.63 | planar 0.35 | 411 |
| Same, before atlas on demand | 3.51 | planar 0.34 | 877 |
| Planar off | 3.12 | | 811 (old atlas) |
| Planar half resolution | 3.27 | planar 0.10 | 846 (old atlas) |
| Probes off | 3.32 | planar 0.34 | 364 |
| Probes and planar off | 2.81-2.89 | | 298 |
| Normalization off | 3.50 | planar 0.34 | 877 (old atlas) |
| 13 probes ("Once"), atlas on demand | 3.66 | planar 0.35 | 504 |
| 13 probes "Always", 2 faces/frame | 3.55 | probes 0.71, planar 0.34 | 459 |
| 1 probe "Always", 2 faces/frame | 3.52 | probes 0.82 | 694 (old atlas) |
| 1 probe "Always", 6 faces/frame | 3.52 | probes 1.69 | 708 (old atlas) |
| 1 probe "Always", legacy (every frame) | 3.53 | probes 3.20 | 708 (old atlas) |

The `gpu` column is the viewport's own render time; probe captures run
before it (in `render_probes()`), so their cost shows in the "probes"
column and in the frame time: 7.13 ms per frame legacy, 4.74 ms with 2
faces per frame (the frame time includes CPU work).

How to read them:
- Planar reflections cost about 0.4 ms in total at full resolution (0.34 ms
  passes, the rest the last frame copy with mipmaps and the shading), 0.16
  ms at half resolution. Independent of scene complexity.
- Probe shading with normalization: about 0.2 ms here; the normalization
  itself 0.02 ms.
- A realtime probe costs a quarter with the default budget (0.82 vs 3.20
  ms), at a third of the refresh rate.
- The atlas: 47 MB for 1 probe (4 layers), 140 MB for 13 (16 layers),
  instead of 512 MB for any number.

Temporal stability (`--anim=0 --tod=0 --frames=400 --measure=60`,
640x360, one run each): flickering pixels 0.12% with planar reflections,
0.19% without, 0.32% with an "Always" probe at 2 faces/frame. All within
the test's known spread (DDGI's own outliers reach 0.6% on an unchanged
build, see kb/mistakes/three-runs-of-a-noisy-test.md): no flicker from the
reflections was measurable, but one run each proves no small difference.
PPR itself is deterministic (no random sampling, no history).

## GPU compatibility

| Tested | Result |
| --- | --- |
| NVIDIA RTX 3060, Vulkan | All features; DDGI on and off |
| NVIDIA RTX 3060, Direct3D 12 (no ray tracing, no DDGI) | Probes, budget, atlas growth, planar reflections; 60-step chaos test clean |
| AMD, Intel, Apple | Not tested (no hardware) |

Requirements: compute shaders, R32_UINT storage images with atomics
(`imageAtomicMax`), RGBA16F storage images, texture copies between 2D
array layers. All are core in Vulkan 1.0, Direct3D 12 and Metal; nothing is
vendor specific. The Mobile and Compatibility renderers get the probe
budget, priority and toggle (renderer-independent code in
`RendererSceneCull`), not tested there; planar reflections and the DDGI
normalization are Forward+ only.

## Test project

The DDGI test project (`misc/ddgi_test_project`) has a `reflect` view (key
7): a showroom with a glossy floor, a wall mirror and a water pool
(`PlanarReflector3D`s), metal spheres and a chrome pillar, a box-projected
`ReflectionProbe`, a skylight, a moving light and a moving ball.

```
bin\godot.windows.editor.x86_64.console.exe --path misc\ddgi_test_project -- --view=reflect --anim=0 --tod=0 --res=1280x720 --frames=180 --shot=C:\tmp\reflect.png
bin\godot.windows.editor.x86_64.console.exe --gpu-profile --path misc\ddgi_test_project -- --view=reflect --res=1920x1080 --frames=150 --bench=300
bin\godot.windows.editor.x86_64.console.exe --path misc\ddgi_test_project -- --view=reflect --res=960x540 --chaos=120 --chaos_refl --extra_probes=6
```

Arguments: `--probes=0|1`, `--probe_faces=N`, `--probe_mode=once|always`,
`--extra_probes=N`, `--norm=0..1`, `--ddgi_ambient=0|1`, `--planar=0|1`,
`--planar_half=0|1`, `--planar_max=N`, `--mirror=0`, `--pool=0`,
`--cam=x,y,z,tx,ty,tz` (another camera), `--msaa=0..3`, `--dump_ts` (with
`--bench`: the GPU timestamps of the last frame). The BENCH line has
`refl_probes` and `planar` pass times with `--gpu-profile`.
`--chaos_refl` adds reflection settings to `--chaos`.

## Build and test results (2026-10-31)

- Editor build (`production=yes`): passes, no new warnings in the changed
  files.
- `reflect` view: Vulkan with DDGI, Vulkan without GI, Direct3D 12; with
  SSR, 4x MSAA, 0.67 3D scale, half resolution planar: no errors.
- Chaos: 120 steps (Vulkan, seed 3) and 60 steps (Vulkan seed 7 with
  `--verbose`, Direct3D 12 seed 11): no errors, no leaked RIDs at exit.
- Legacy and budgeted probe captures give the same image; the main probe's
  image is unchanged after the atlas grows from 4 to 16 layers.
- Class reference: `doc/tools/make_rst.py` reports no errors for the new
  entries (the errors it lists are in the PhysX module docs and older).
- Smoke tests (`devtools	est\smoke.ps1`): all pass (PhysX game and
  editor, path tracer, DDGI interior: flicker 0.00%).

## Known limitations

- Planar reflections show only what is on screen. Mirrors facing the
  camera, and parts of objects hidden behind others, fall back to probes
  and the sky. There is no mirrored-camera mode yet (see the steps).
- The reflected image is last frame's, reprojected: fast moving objects
  lag by a frame in it; disocclusions show the probe.
- Normal-mapped reflections are approximated by shifting the lookup, not
  by tracing the bent ray (good for water, not for strong bumps).
- Planar reflections: Forward+ only, one view (not in XR), at most 8
  reflectors per view; overlapping transparent reflectors pick the
  nearest.
- Probe captures don't contain DDGI's indirect light (they use the
  environment ambient); normalization fixes their overall level at the
  shading point, not the light distribution inside the capture.
- Normalization assumes the probe's light is like the surface's: a probe
  that sees a bright window can be dimmed or brightened unevenly at
  surfaces with very different surroundings. Lower
  `probe_ddgi_normalization` there.
- "Always" probes refresh at 6 / budget frames per update at best, shared
  by all realtime probes.
- The Mobile and Compatibility renderers weren't tested with the probe
  budget; the editor gizmo was compiled but not checked by eye.
- Stock Godot behaviors that the test scene had to work around (not
  changed): a probe's capture far plane is its box face (set
  `max_distance` when walls sit on the box), and `blend_distance` fades a
  probe to zero at its box faces.

## Steps

| # | Step | Status |
| --- | --- | --- |
| 1 | Audit: renderer, Godot probes, DDGI interplay, SSR path | Done |
| 2 | Research and architecture | Done |
| 3 | Probes: budget, priority, toggle | Done |
| 4 | Probes: DDGI normalization, DDGI over probe ambient | Done |
| 5 | Planar reflections: API, node, gizmo, PPR passes, compositing | Done |
| 6 | Atlas on demand, LRU fix | Done |
| 7 | Test view, benchmarks, chaos, docs | Done |
| 7b | Automatic reflections from the DDGIVolume (`auto_reflections`) | Done |
| 8 | Optional mirrored-camera mode for mirrors that face the camera (budgeted, low resolution) | Open |
| 9 | DDGI irradiance inside probe captures (sample the probes in the capture pass) | Open |
| 10 | Test on AMD and Intel GPUs, Mobile and Compatibility renderers | Open |

## Sources

- A. Cichocki. "Optimized pixel-projected reflections for planar
  reflectors." SIGGRAPH 2017, Advances in Real-Time Rendering.
  https://www.advances.realtimerendering.com/s2017/PixelProjectedReflectionsAC_v_1.92_withNotes.pdf
  (implemented from the talk; no code taken).
- D. Lazarov. "Getting More Physical in Call of Duty: Black Ops II."
  SIGGRAPH 2013, Physically Based Shading course (environment map
  normalization).
  https://blog.selfshadow.com/publications/s2013-shading-course/lazarov/s2013_pbs_black_ops_2_notes.pdf
- S. Lagarde, C. de Rousiers. "Moving Frostbite to Physically Based
  Rendering 3.0." SIGGRAPH 2014 course notes (cubemap renormalization,
  specular occlusion).
  https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf
- S. Lagarde, A. Zanuttini. "Local image-based lighting with
  parallax-corrected cubemaps." SIGGRAPH 2012 talk (box projection, as in
  Godot's probes).
- B. Wronski. "Assassin's Creed 4: Black Flag - Road to next-gen
  graphics." GDC 2014 (relit, normalized probes).
  https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf
- Unity: reflection probe time slicing ("All Faces at Once", "Individual
  Faces"). https://docs.unity3d.com/Manual/RefProbePerformance.html ;
  Adaptive Probe Volumes reflection probe normalization (lower/upper clamp):
  https://docs.unity.cn/Packages/com.unity.render-pipelines.core@15.0/api/UnityEngine.Rendering.ProbeVolumeShadingParameters.html
- Unity HDRP Planar Reflection Probe (re-render, "Every Frame" / "On
  Demand"). https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition%4011.0/manual/Planar-Reflection-Probe.html
- Unreal Engine planar reflections (re-render; screen percentage scales
  pixel cost, not triangle cost).
  https://dev.epicgames.com/documentation/en-us/unreal-engine/planar-reflections-in-unreal-engine
- Earlier notes in the knowledge base: Ghost of Yotei relit probes
  (`kb/research/ghost-of-yotei-ambient.md`), FF7 Rebirth reflection leaks
  (`kb/research/ff7-rebirth-gi.md`), the specular occlusion proposal
  (`kb/ddgi/improvements/specular-occlusion.md`).

## Findings log

Newest first.

- **2026-10-31, profiler limit.** Six probe faces per frame overflow the
  default 256 GPU timestamps under `--gpu-profile` (ERROR lines, lost
  timings). The test project now sets
  `debug/settings/profiler/max_timestamp_query_elements=2048`.
- **2026-10-31, benchmark harness.** In PowerShell, `$a = if (...) {
  $s.Split(' ') }` unrolls a one-element array to a string, and `@a` then
  passes nothing: the single-flag runs measured the defaults (planar "off"
  still showed the planar pass). Cast with `[string[]]`. See
  kb/mistakes/powershell-single-arg-splat.md.
- **2026-10-31, atlas memory.** The stock atlas allocates `reflection_count`
  layers when the first probe renders: 512 MB at the defaults (256, 64
  probes), 694 MB in realtime mode. On demand: 47 MB for one probe.
- **2026-10-31, test scene.** The test probe showed the sky: the room's
  walls lay exactly on the probe box faces, which are the capture's far
  plane, and `blend_distance` (1 m) faded the probe to zero on the floor
  and the mirror. Stock behavior; the test sets `max_distance` and
  `blend_distance = 0`.
- **2026-10-31, overlapping reflectors.** A pool reflector above a floor
  reflector: the floor's reflections were written into the pool's pixels
  (the basin lay within the floor's tolerance). Writes are now rejected
  when another reflector lies in front along the camera ray.
- **2026-10-31, reflectors reflected themselves.** First test: the floor
  and the mirror showed black or their own image. Pixels on the reflector
  plane mirror onto themselves with offset 0 and win every atomic.
  Points within the reflector's thickness are now skipped. Found with a
  debug output of the decoded source position (a 4x4 UV grid lined up with
  the screen).
