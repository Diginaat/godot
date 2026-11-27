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
| 1 | D3D12 baseline and after | done |
| 2 | Free compiled shader bytes and the loaded Vulkan PSO cache after use | done |
| 2 | Skip precompiling transparent pipelines that can't be drawn (TRANSPARENT+MV/SEP) | done |
| 2 | Fix: SDFGI pass uniform set lacked bindings 47/48 (found by the SDFGI config) | done |
| 2 | Pipeline hash map keyed by the full key (collision fix) | done |
| 2 | Per-shader skipped variants: own depth of shared-shadow shaders; depth, MV and SEP color of alpha-only shaders | done |
| 3 | Measure ubershader vs specialized pipeline cost (`FORWARD_PIPELINE_STATS`) | done |
| 3 | Specialized-first compile scheduling for the first draw | proposal (see below) |
| 3 | Split the Forward+ variant groups by feature | done |
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
first, then warm, is also what a user sees. Run a build's warm runs right
after its own cold run: another build's cold run clears the driver cache
for every exe (a `prune` control run after the `fullkey` cold run stalled
1.53 s instead of 0.73 s).

### Results (RTX 3060, 12 threads, Vulkan, `-Mv`, 120 materials)

| Build | Run | Add frame stall | Variants (built/used per shader) | GLSL to SPIR-V CPU | Shader create CPU | Pipelines (CPU, max) | Frame p50 / GPU p50 | Memory end / peak |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base | cold | 145.8 s | 3556 scene (28/6) | 236.7 s | 22.6 s | 783 (1577 s, 7.1 s) | 2.90 / 1.71 ms | 209 / 221 MB |
| base | warm x3 | 2.40-2.42 s | 3556 cached | - | 27.4 s | 783 (2.8 s, 16 ms) | 1.82 / 1.65 ms | 353 / 356 MB |
| buf | cold | 145.6 s | same | 238.7 s | 23.1 s | 783 (1573 s, 6.9 s) | 2.32 / 1.73 ms | 100 / 212 MB |
| buf | warm x2 | 2.41-2.44 s | same | - | 27.7 s | 783 (2.8 s, 17 ms) | 1.85 / 1.65 ms | 101 / 190 MB |

| groups | cold | 128.0 s | 762 scene (6/6) | 57.6 s | 5.4 s | 783 (1567 s, 7.0 s) | 2.62 / 1.72 ms | 85 / 116 MB |
| groups | warm x2 | 0.77-0.78 s | 762 cached | - | 6.7 s | 783 (2.85 s, 24 ms) | 1.90 / 1.65 ms | 85 / 164 MB |
| prune | cold | 96.1 s | 762 | 59.2 s | 6.0 s | 720 (1175 s, 6.9 s) | 2.29 / 1.95 ms | 85 / 122 MB |
| prune | warm x2 | 0.72-0.74 s | 762 cached | - | 6.9 s | 720 (2.26 s, 20 ms) | 1.86 / 1.66 ms | 85 / 139 MB |
| fullkey | cold | 96.4 s | 762 | - | - | 720 (1176 s, 6.8 s) | 2.34 / 1.73 ms | 85 / 119 MB |
| fullkey | warm x3 | 0.72 s | 762 cached | - | - | 720 (2.30 s, 25 ms) | 1.86 / 1.65 ms (CPU 0.69) | 85 / 139 MB |
| skip4 | cold | 94.7 s | 472 scene | 40.2 s | 3.9 s | 720 (1177 s, 6.8 s) | 2.39 / 1.73 ms | 83 / 114 MB |
| skip4 | warm x2 | 0.65-0.66 s | 472 cached | - | 4.2 s | 720 (2.31 s, 20 ms) | 1.86 / 1.66 ms (CPU 0.68) | 84 / 139 MB |

`buf` = base + freed variant bytes + Vulkan PSO buffer trim. `groups` =
buf + feature groups. `prune` = groups + no transparent MV/SEP pipeline
precompiles (+ the SDFGI binding fix, which doesn't affect this scene).
Shader cache on disk: 104 MB (base) -> 25.5 MB (groups). Vulkan PSO cache:
156 -> 130 MB (prune). Cold GPU p50 differs run to run while specialized
pipelines still compile in the background; warm GPU p50 is unchanged.

### D3D12 (same scene, `-Driver d3d12 -Mv`)

| Build | Run | Add frame stall | GLSL to SPIR-V | SPIR-V to DXIL | Render thread wait | Pipelines (CPU) | Frame p50 / CPU / GPU | Memory end / peak | Shader cache |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base | cold | 238.7 s | 262 s | 1846 s | 166 s | 783 (905 s) | 5.00 / 1.63 / 1.78 ms | 548 / 561 MB | 194 MB |
| base | warm x2 | 0.65-0.67 s | - | - | 0.02 s | 783 (1.68 s) | 3.28 / 1.05 / 1.46 ms | 547 / 547 MB | - |
| fullkey | cold | 97.1 s | 65 s | 509 s | 49 s | 720 (689 s) | 4.65 / 1.51 / 1.78 ms | 154 / 188 MB | 49 MB |
| fullkey | warm x2 | 0.48-0.49 s | - | - | 0.02 s | 720 (1.51 s) | 2.38 / 0.83 / 1.45 ms | 153 / 196 MB | - |
| skip4 | cold | 96.1 s | 43 s | 370 s | 4.2 s | 720 (821 s) | 4.13 / 1.51 / 1.77 ms | 133 / 171 MB | 34 MB |
| skip4 | warm x2 | 0.46 s | - | - | 0.0 s | 720 (1.47 s) | 1.59 / 0.61 / 1.44 ms | 133 / 163 MB | - |

D3D12 converts every variant to DXIL (7x the GLSL compile), so fewer
variants matter more there. The D3D12 cold stall is pipeline-bound
after the groups (skip4 didn't move it). skip4's cold pipeline CPU (821 s
against 689 s) is unexplained: the change doesn't touch pipelines, and 10
files of NVIDIA's DXCache were held open by another application in every
D3D12 cold run, so that cache wasn't fully cold. One run each; not
repeated. D3D12 has no Godot PSO cache (`cache_kb=0`).

Image checks (`devtools/test/image_diff.py`, last frame): base vs groups vs
prune pixel-identical (max 0) for `-Mv`, no motion vectors, `-Mv -Ssr`,
`-Mv -VoxelGI`; `-Mv -Sss -Dp -Sdfgi` identical for groups and differs by
0.6% of pixels for prune because the SDFGI fix makes SDFGI voxelize the
scene (base and groups logged 48 errors, 12 failed SDFGI draws; prune 0).
No run logged the group safety net. Smoke passed on every build. Cold stall is
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
- D3: feature groups instead of BASE/ADVANCED: a variant's group is the set
  of features it needs (separate specular, lightmap, motion vectors, dual
  paraboloid, normal-roughness, VoxelGI, material, SDF, multiview); group 0
  needs none. `enable_shader_features()` enables every group whose features
  are all on. Enabled where the renderer sets the matching pipeline
  requirement (`_update_dirty_geometry_pipelines`) and in the passes that
  need them (MV color, separate specular/lightmap/VoxelGI before the passes,
  shadow DP, material bake, SDFGI, path tracer). Safety net: a request for a
  variant of a disabled group enables that group (one stall, logged with
  `--verbose`) instead of returning a placeholder. Shader baking enables
  every feature. Trade-off: a feature turned on later (SSR in a settings
  menu) compiles its groups then; before, any advanced feature compiled all
  of them up front. Warm (cached) that costs a cache load.
- D4: transparent pipelines are precompiled without MV/SEP, matching both
  transparent passes. Any other request still compiles on demand.
- D5: `PipelineHashMapRD` keyed by the whole key (`HashMap<Key, RID>`,
  `HashSet<Key>` for submitted compiles); each key has `operator==` over the
  fields its `hash()` covers. The draw loops compare the previous key instead
  of the previous hash (no murmur hash per draw). Applies to Forward+,
  Mobile, canvas and the ray tracing scene shader.
- D6: per-version skipped variants in ShaderRD
  (`version_set_skipped_variants()`, a 64-bit mask set before
  `version_set_code()` and part of the cache file name). Groups compile,
  cache and load without them; `version_get_shader()` compiles a skipped
  variant on demand (logged with `--verbose`, not cached) if one is asked
  for. Forward+ skips, per shader: plain depth (+DP, multiview) for shaders
  that use the shared shadow material; every depth variant but the material
  one for alpha-only shaders without depth; color with motion vectors or
  separate specular for alpha-only shaders. The default shader keeps all
  (`initializing_default_shader`). The material uniform set takes its layout
  from the color ubershader instead of depth variant 0. `uses_blend_alpha`
  and the alpha-to-coverage blend mode are now set before the mask (they
  were set after `version_set_code`, and `uses_alpha_pass()` reads them).
  Result: variants built equal variants used except color with/without
  motion vectors of opaque shaders (depends on whether instances move).
- D2: free each group's compiled bytes at the end of `_compile_version_end`:
  nothing reads them later (a new group or new code compiles or loads again).

## Proposal: specialized-first scheduling (not implemented)

`FORWARD_PIPELINE_STATS` (cold, Vulkan, 120 materials): 379 specialized
pipelines took 113 s of driver CPU (0.30 s each), 329 ubershader pipelines
1067 s (3.24 s each, 11x). The add frame waits for the ubershader
pipelines, so the cold stall is about ubershader CPU / threads. Waiting for
the specialized pipeline instead could cut that stall to roughly a tenth,
but needs three changes, each with a cost:

1. Ubershader precompiles at low priority, so the waited-on specialized
   compiles aren't queued behind them (the pool queue is FIFO).
2. Submit the frame's specialized compiles before waiting on any (the draw
   loop now requests them one draw at a time, which would wait serially).
3. Accept ~1000 s of ubershader CPU running on the low-priority threads for
   minutes after the stall, and a short stall (~0.3 s) when a draw needs a
   new specialization before its ubershader is ready.

Upstream chose ubershader-first for no stutter after load; scenes loaded
with the resource loader compile their ubershader pipelines during the
load (`mesh_generate_pipelines`), not in a frame. Decide with the user.

## Log

- 2026-11-17: audit, instrumentation, benchmark, Vulkan baseline; freed
  shader bytes and PSO buffer (warm memory 353 -> 101 MB, no speed change).
- 2026-11-17: feature groups (variants 28 -> 6 per shader here; warm stall
  2.42 -> 0.77 s, cold 145.6 -> 128.0 s, cache 104 -> 25.5 MB); transparent
  pipeline pruning (cold 128.0 -> 96.1 s, pipeline CPU -25%); SDFGI binding
  fix. Next: pipeline hash map key, D3D12 numbers, shared-shadow depth
  variants, specialization compile priority during the stall.
- 2026-11-17: full-key pipeline maps; D3D12 numbers (cold 238.7 -> 97.1 s);
  per-shader skipped variants (Vulkan variants 762 -> 472, warm stall 0.72
  -> 0.65 s); ubershader vs specialized pipeline cost measured, scheduling
  change written up as a proposal.
- 2026-11-27: smaller ubershader pipelines (instance data loaded once per
  stage, DDGI probe capture as a pipeline constant): RTX 5070, `-Mv`,
  ubershader driver CPU 545 -> 496 s, cold stall 48.4 -> 44.3 s
  ([kb/project/ubershader-cold-compile.md](../../kb/project/ubershader-cold-compile.md)).
