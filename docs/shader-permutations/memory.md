# Shader permutations and pipelines: working memory

Branch `feat/shader-permutations` (from `dev` 690fce897b), worktree
`GodotPhysxDev-core`. Goal: fewer unneeded shader variants and pipelines,
less compile stutter, same output. Read this first when resuming.

## Checklist

| Phase | Step | State |
| --- | --- | --- |
| 1 | Audit of ShaderRD, Forward+ variants, pipeline maps | done |
| 1 | Instrumentation: `SHADER_STATS`, `SHADER_VARIANTS`, `PIPELINE_STATS` (with `--verbose`) | done |
| 1 | Benchmark `devtools/test/shader_bench.ps1` + `shader_bench.tscn` | done |
| 1 | Baseline numbers, Vulkan, motion vectors (cold, warm) | done |
| 1 | Baseline: no motion vectors, SSR, D3D12 | planned |
| 2 | Free compiled shader bytes and the loaded Vulkan PSO cache after use | done |
| 2 | Skip precompiling transparent pipelines that can't be drawn (TRANSPARENT+MV/SEP) | planned |
| 2 | Pipeline hash map keyed by the full key (collision fix) | planned |
| 2 | Skip unused depth variants of shaders that use the shared shadow material | planned |
| 3 | Split the Forward+ advanced group by feature | planned |
| 4 | Validation: smoke, image compare, DDGI/PT, D3D12 | planned |

## How the system works (audit)

- **ShaderRD** (`servers/rendering/renderer_rd/shader_rd.cpp`): one
  `Version` per material shader. Variants are grouped; a group compiles as a
  whole (all its variants, worker threads) when it is enabled, and its cache
  file holds all variants (`<name>/<group sha256>/<code sha1>.<api>.cache`).
  A caller that needs a variant of a group still compiling waits
  (`_compile_version_end`), which is the stall at first use.
- **Forward+ scene shader** (`scene_shader_forward_clustered.cpp`): 50
  variants per material shader: 9 depth modes x {specialized, ubershader} and
  32 color variants (ubershader x separate specular x lightmap x multiview x
  motion vectors). Groups: BASE (always: depth, depth DP, depth+normal-roughness,
  plain color; each x2 = 8), ADVANCED (VoxelGI depth, material depth, SDF depth,
  and every color variant with separate specular, lightmap or motion vectors =
  20), MULTIVIEW, ADVANCED_MULTIVIEW.
- ADVANCED is enabled by any of: motion vectors (TAA, FSR2, DLSS), SSR/SSS
  (separate specular), lightmaps, VoxelGI, SDFGI, material bake passes, the
  path tracer. This fork almost always runs with motion vectors (DLSS), so
  every material shader compiles 28 variants.
- **Pipelines** (`pipeline_hash_map_rd.h`): per shader, ubershader pipelines
  are precompiled at surface creation (`_mesh_compile_pipelines_for_surface`)
  for every pass/format the global state needs; specialized pipelines (spec
  constants: soft shadows, multimesh flags, GPU instances, DDGI capture...)
  compile in the background on first draw while the ubershader draws.
- Depth and shadow passes of shaders with `uses_shared_shadow_material()`
  (opaque, no vertex/discard/depth changes) draw with the **default**
  material's shader (`_geometry_instance_add_surface_with_material`), so
  those shaders' own `DEPTH_PASS` and `DEPTH_PASS_DP` variants look unused.
- Shader baker export (`shader_baker_export_plugin.cpp`) enables
  `FEATURE_ADVANCED_BIT` and bakes every enabled group generically.

## Problems found

1. **Pipeline map keyed by a 32-bit hash only.** `PipelineHashMapRD` stores
   and looks up pipelines by `Key::hash()`; two keys with the same hash get
   the same pipeline (wrong formats, blend or spec constants), silently.
   Rare (n^2/2^33 per shader) but a correctness bug. `PipelineCacheRD`
   compares full keys and is fine.
2. **Coarse ADVANCED group.** Motion vectors alone pull in lightmap,
   separate-specular, VoxelGI, SDF and material-bake variants.
3. **Unused depth variants** for shared-shadow-material shaders (to be
   confirmed by `SHADER_VARIANTS` request counts).
4. **Compiled bytes kept forever.** `Version::variant_data` holds every
   variant's shader container after the GPU shader exists; it is only read
   to create the shader and to write the group's cache file, both done by
   the end of `_compile_version_end`. Planned: free a group's bytes there
   (measure with `mem_mb` in the benchmark line).
5. **Vulkan PSO cache file kept in RAM.** The driver kept the loaded
   `pipelines.*.cache` (153 MiB here) after `vkCreatePipelineCache` copied
   it, and a full-size buffer after every save; the saved file could also be
   longer than its `data_size` (rejected on the next load). Fixed: keep only
   the header, save exactly `data_size`.
6. **Transparent pipelines precompiled for impossible passes.** Both
   transparent passes (raster and path tracer) strip motion vectors and
   separate specular, but `_mesh_compile_pipelines_for_surface` precompiles
   TRANSPARENT+MV, +SEP, +SEP+MV ubershader pipelines.
7. **D3D12 has no app-side pipeline cache** (`pipeline_cache_create` returns
   false; only the driver's DXCache). Later candidate: ID3D12PipelineLibrary.
8. Forward Mobile: 10 variants per group, lightmap always included; lower
   priority (desktop fork).

## Measurements

See the results table below; numbers are from `shader_bench.ps1` runs.

**Cold** means every cache layer is emptied before each run:
`user://shader_cache`, `user://vulkan/pipelines.*`, the project's baked
`res://.godot/shader_cache` (Godot falls back to it), and the GPU driver's
own disk caches (`%LOCALAPPDATA%\NVIDIA\GLCache` for Vulkan,
`NVIDIA\DXCache` and `D3DSCache` for D3D12, AMD/Intel equivalents). The
driver caches survive `--clear-shader-cache`; without clearing them a "cold"
run only measures Godot's GLSL compile, not the driver's pipeline compile.
The script warns if a file is held open by another application, and fails a
cold run whose `SHADER_STATS: TOTAL` line shows any `cached` variant.
**Warm** runs follow one unrecorded priming run.

**Protocol: every build gets a cold run before its warm runs.** NVIDIA's
driver cache is per executable and is only filled when the driver compiles a
pipeline itself. A new exe whose Godot PSO cache is already full never fills
it, and its "warm" runs stay slow: an identical copy of the baseline under a
new name took 20.5 s of pipeline CPU instead of 2.7 s, for five runs. Cold
first, then warm, is also what a user sees.

### Results (RTX 3060, 12 threads, Vulkan, `-Mv`, 120 materials)

| Build | Run | Add frame stall | Variants (built/used per shader) | GLSL to SPIR-V CPU | Shader create CPU | Pipelines (CPU, max) | Frame p50 / GPU p50 | Memory end / peak |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base | cold | 145.8 s | 3556 scene (28/6) | 236.7 s | 22.6 s | 783 (1577 s, 7.1 s) | 2.90 / 1.71 ms | 209 / 221 MB |
| base | warm x3 | 2.40-2.42 s | 3556 cached | - | 27.4 s | 783 (2.8 s, 16 ms) | 1.82 / 1.65 ms | 353 / 356 MB |
| buf | cold | 145.6 s | same | 238.7 s | 23.1 s | 783 (1573 s, 6.9 s) | 2.32 / 1.73 ms | 100 / 212 MB |
| buf | warm x2 | 2.41-2.44 s | same | - | 27.7 s | 783 (2.8 s, 17 ms) | 1.85 / 1.65 ms | 101 / 190 MB |

`buf` = base + freed variant bytes + Vulkan PSO buffer trim. Cold stall is
dominated by the driver building pipelines (1577 s CPU over 12 threads);
warm stall by creating 3556 shader variants from the cache (7.7 ms each).
Scene shader variants used in this scene: 0 (depth: material uniform set
layout, 123 shaders), 9 (depth ubershader, 50: shaders with their own
depth), 18/19 (color, 64/121), 34/35 (color + motion vectors, 58/121).
Never used: dual paraboloid, normal-roughness, VoxelGI, material, SDF, all
separate-specular and lightmap color variants (22 of 28).

## Decisions

- D1: measure with engine counters printed at exit (`--verbose`), no new
  public API. Counters cost one mutex per variant compile and an atomic add
  per pipeline, nothing per draw.
- D2: free each group's compiled bytes at the end of `_compile_version_end`:
  nothing reads them later (a new group or new code compiles or loads again).

## Log

- 2026-11-17: audit, instrumentation, benchmark, Vulkan baseline; freed
  shader bytes and PSO buffer (warm memory 353 -> 101 MB, no speed change).
