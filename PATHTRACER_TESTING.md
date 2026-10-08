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
| Emission (color, energy, texture) | Supported on hit only. No light sampling (NEE) toward emissive meshes, so small emitters are noisy | `shade_and_bounce()` |
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
| 3 | Baseline: screenshots of every view, path traced and raster; list every visible difference in the findings log | Next |
| 4 | Fix PBR and lighting differences found in step 3 | |
| 5 | Emission: light sampling toward emissive meshes so emitters light the scene without heavy noise | |
| 6 | Custom shaders: fix what step 3 shows (vertex displacement, TIME, alpha) | |
| 7 | Volumetric fog in the path tracer (Environment volumetric fog, then FogVolume) | |
| 8 | Glass: alpha blend, refraction and transmission | |
| 9 | Clean up, document, merge `dev` into `nvidia-pt-dlss` | |

## Findings log

Newest first. Note the date, the commit, the view and what you saw or changed.

- 2026-10-08: Steps 1 and 2 done. No fixes yet.
