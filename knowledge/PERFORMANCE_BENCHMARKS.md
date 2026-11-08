# Performance and validation

No Godot-vs-reference-engine performance result exists from this work. The
reference engine's historical natural-scene results use simplified lighting/materials and distance policies;
they are **not** comparable to a fully lit Godot scene. Do not use FPS alone.

## First-slice validation plan

Exercise real RD command buffers without drawing unbounded pre-fix commands.
Inspect every surface's five-word command after explicit count, zero, full count
and `-1` transitions. Test GPU-written bounded counts independently from CPU
setters. Free all RIDs and require clean exit. Then exercise ordinary material
rendering with a bounded GPU count, with a hard `--quit-after` limit.

Build and focused tests: passed. Existing editor before rebuild identifies as
`4.8.beta.custom_build.ef52b88b2`; this is not source HEAD `b23737c64e`.
`git diff --stat ef52b88b26 b23737c64e` contains README/images only, so the
prebuilt reproducer differs from the source baseline only in documentation.

### Reproducer and runner

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/large_world.ps1 -Driver vulkan
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/large_world.ps1 -Driver d3d12
```

The execution-policy flag applies only to the runner process; no machine policy
is changed. Runner writes `devtools/out/large_world/indirect_<driver>.log`, checks
exit code, script/engine errors, failure markers, resource leaks and required pass
marker. `--quit-after 300` bounds malformed tests. The test scene has eight
potential instances and two ordinary material surfaces (indexed/non-indexed).
It asserts every word of both command records, preserves CPU sentinel semantics,
then a test-only compute producer writes zero, oversized-request-clamped-to-eight
and three instances. Finally the normal renderer must produce exactly the first
three red/green markers, with background at remaining instance positions.
Readbacks deliberately synchronize **only for test assertions**, not a proposed
runtime culling design. This first-slice workload does not implement a culler;
the next section records the subsequent native frustum-culling implementation.

### Initial evidence

- Before the C++ patch, Vulkan / RTX 3060 / existing `ef52b88b2` editor: failure
  reproduced. Both surfaces contained `4294967295` at instance-count word after
  each `-1` transition; expected allocated capacity `8`. The unsafe commands were
  never attached to a scene instance or submitted for drawing. Log was captured
  by the runner; later runs overwrite that log.
- Default `python -m SCons` could not configure: Python 3.11 lacks SCons. Python
  3.14 has SCons 4.11.1; build uses `py -3.14 -m SCons` with identical flags.
- Build passed: `py -3.14 -m SCons -k -j10 platform=windows target=editor production=yes`,
  MSVC 14.5 / Windows SDK 10.0.26100.0, SCons elapsed 2m29.21s. Existing ANGLE
  dependency warning and DDGI editor C4458 warnings occurred; build was not
  warning-free. No unrelated warnings were patched.
- Post-patch Vulkan: passed on NVIDIA GeForce RTX 3060, Vulkan 1.4.357,
  Forward+; binary `4.8.beta.custom_build.b23737c64` plus working-tree patch.
- Post-patch D3D12: passed on the same GPU, D3D12 12_0, Forward+, same binary.
- Both runs returned 0, emitted `INDIRECT_TEST_PASS`, and had no detected
  script/engine failures or resource-leak diagnostics. All count/layout and
  red/green/background pixel assertions passed at 640×360.
- Existing standard smoke suite passed via
  `powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/smoke.ps1`:
  PhysX GPU game, PhysX GPU editor, Vulkan path-tracer screenshot and DDGI
  interior measurement. This is the standard editor suite, not .NET coverage.
- `git diff --check` and new OKF concept frontmatter checks pass.

These are correctness checks at eight potential instances, **not** scalability,
performance, GPU-culling, HZB, imported-LOD, shadow or multi-view results.

## Native GPU frustum-culling slice — 2026-11-05

The same runner now runs both `main.tscn` (the original indirect regression) and
`culling.tscn`. Culling logs: `devtools/out/large_world/culling_<driver>.log`.
Conditions: same RTX 3060, Vulkan 1.4.357 / D3D12 12_0, 640×360 main viewport,
320×180 secondary viewport resized to 384×216, default AA off. Test-only readbacks
are assertions; production culling has no feedback readback.

| Check | Vulkan | D3D12 |
| --- | --- | --- |
| Original indirect count/layout/material regression | Pass | Pass |
| Compile and execute actual native cull shader from source | Pass | Pass |
| 65 inputs / partial workgroup; empty draw; independent output ranges | Pass | Pass |
| Exact expected visible source IDs, no duplicates, untouched output guards | Pass | Pass |
| Non-uniform/reflected/sheared transform rows; current-history offset | Pass | Pass |
| Plane-tangent retained, definitely outside instance rejected | Pass | Pass |
| Native opaque + depth raster, indexed/non-indexed materials, ID/color/custom data | Pass | Pass |
| Culled/unculled full-image byte equality at 65 and 100000 potential instances per object | Pass | Pass |
| Two transformed objects sharing one MultiMesh resource | Pass | Pass |
| Two simultaneous viewports with distinct cameras; orthogonal/perspective | Pass | Pass |
| Timestamp evidence of actual native culling in both viewports (not silent fallback) | Pass | Pass |
| Secondary viewport resize and MultiMesh resource replacement while running | Pass | Pass |
| No detected script/engine failures or resource-leak diagnostics; required marker, exit 0 | Pass | Pass |

Both objects have two surfaces and share their source transform buffer. Native
visibility is currently per surface, so “100000 potential instances” is not a
claim of only one dispatch or only 100000 total GPU tests across objects/views.
The initial implementation used unordered atomics. Follow-up stable compaction
replaces that order with block-prefix scan/scatter; the 100k fixture now contains
overlapping coplanar IDs with different color/custom data, and asserts original
source-ID order directly without sorting the kernel result. Both backend
matrices pass. Transparency still stays conventional.

Final rebuild passed using the unchanged production flags and configured Python
3.14 interpreter (SCons elapsed 39.96s). Standard PhysX/PT/DDGI smoke rerun passes
(PhysX GPU game/editor, Vulkan PT screenshot, DDGI interior). No
timing/speedup/percentile comparison was performed; 100k is a
correctness workload, not a measured performance claim. Motion-vector imagery,
off-camera shadow equivalence with culling enabled, budget exhaustion, device
loss, Metal, Mobile, Compatibility, XR and double builds have not been tested.

Bring-up failures were isolated and fixed: new C++ TU needed explicit MeshStorage
include, `Math::next_power_of_2` and the cache API's LocalVector; GDScript's typed
array conditional caused a fixture runtime error (caught by runner despite exit
0). Corrected fixture uses explicit typed `assign()` and releases all RIDs.

Static validation: ProjectSettings-filtered class-reference check passes. Full
class-reference check fails on five errors in unchanged PhysX XML (cutout param,
fracture method markup and three Light3D member links), with two warnings and
existing code-sample parity reports. C++ files and new GLSL pass clang-format;
whole shared-include formatting reports two existing DDGI block-layout issues,
also reproduced directly from HEAD. Those unrelated files/blocks were not fixed.

## Native cluster cooker/resource slice

Standalone CPU correctness test (headless is appropriate for this non-GPU part):

```powershell
bin/godot.windows.editor.x86_64.console.exe --headless --path misc/large_world_test_project --quit-after 300 res://clusters.tscn
```

The fixture builds a 48×48 curved grid with normals/UVs, calls native
MeshClusterData.generate, checks complete non-overlapping group cuts at five
thresholds, checks mesh boundary edges (including mixed-detail seams), verifies
source arrays unchanged, exercises ResourceSaver/Loader and corrupt-cache
rejection. Initial pass: 13,824 source indices, 82 groups, 127 clusters, 3,456
coarse-cut indices. No frame-time comparison or GPU CLOD rendering is claimed.
Extreme finite projection inputs and stricter orphan/range validation also pass;
production build, headless and Vulkan/D3D12 runs pass. First compile required an
explicit ClassDB include in the new resource TU; corrected locally.

Follow-up review found that the initial ID-remap macro changed the GLSL value
type to `uint`. Corrected to preserve shader-language `INSTANCE_ID` as `int`;
the culling fixture now passes it to an `int` function and uses signed arithmetic.
Production build and Vulkan/D3D12 focused matrices pass with that regression.

## Visual large-world scenes — 2026-11-06

Two scene tests built only from ordinary Godot nodes, with GPU MultiMesh culling,
resident CLOD and current-frame HZB enabled. They are correctness/coverage checks
with screenshots, **not benchmarks**.

- `forest.tscn` / `devtools/test/forest.ps1`: 6144 MultiMesh trees, 48 CLOD
  rocks, directional and two shadowed omni lights. Two views must be
  pixel-identical between source rendering and the enabled path (CLOD error
  0 px), and light/shadow negative controls must change the image.
- `forest_map.tscn` (day) and `forest_night.tscn` (night) /
  `devtools/test/forest_map.ps1 -Variant day|night`: 720×720 m valley, 115k-triangle
  CLOD terrain, about 25.8k trees in 48 m MultiMesh patches, 39k grass tufts and
  2.9k flowers (visibility ranges, no shadows), 280 CLOD rocks, SSR water.
  Day: procedural sky, depth fog, SSAO, SSR, glow, ACES. Night: volumetric fog,
  lake FogVolume, 68 local lights (shadowed campfire, two cabins, searchlight),
  emissive lanterns/windows/mushrooms/crystals and 2500 vertex-animated fireflies
  that must stay on the conventional path. FXAA keeps HZB eligible.

Smoke conditions: RTX 3060, Vulkan 1.4.357 and D3D12, 1280×720, editor build of
`63c829ebaa` plus these test files. Eight route views must differ, every GPU stage
must dispatch in at least one view (all three dispatched in all eight), and no
stage may dispatch with GPU geometry disabled. Night also requires bright
emissive coverage and a changed image when local lights or volumetric fog are
switched off. All four runs and both dense-forest runs passed.

The smoke prints one informational GPU time at one forest-road view (60 frames
each, single run): day ≈8.0 ms enabled versus ≈8.1 ms disabled, night ≈6.0
versus ≈6.1 ms, on both backends. That ~1% gap is within the noise of a single
run; this records no performance improvement. The scene is mostly
GPU-bound by conventional shadows, grass and post effects, not by instance
submission.

First run found a test bug: timestamps were read after `get_image()`, which
stalls the device and replaces the captured results (see
`kb/mistakes/timestamps-after-texture-readback.md`).

## Valley benchmark and CLOD multi-draw — 2026-11-06

`devtools/test/forest_bench.ps1 -Driver vulkan|d3d12 -Label <name>` runs the day
valley at three fixed route views (t = 0, 27, 40.5 s). Each of four modes
(source; GPU culling; culling + CLOD; culling + CLOD + HZB) is warmed, then
measured twice in alternating order over 240 frames each. Values are p50
viewport render-thread CPU time plus shared frame setup, and viewport GPU time,
in ms. Conditions: RTX 3060, 1280×720, FXAA, VSync off, editor production build,
same machine and session. CLOD error threshold 1 px. Shadows, SSAO, SSR, glow
and fog stay on, so GPU time is mostly conventional work.

Before (`63c829ebaa`, one indirect call per CLOD group record):

| View | Backend | source CPU/GPU | culling | +CLOD | +HZB |
| --- | --- | --- | --- | --- | --- |
| 0 | Vulkan | 0.788 / 3.691 | 0.880 / 3.408 | 1.118 / 3.462 | 1.180 / 3.544 |
| 1 | Vulkan | 1.140 / 8.094 | 1.407 / 7.891 | 1.717 / 7.927 | 1.796 / 8.046 |
| 2 | Vulkan | 0.874 / 5.340 | 1.055 / 5.066 | 1.313 / 4.984 | 1.344 / 5.083 |
| 0 | D3D12 | 0.710 / 3.592 | 0.778 / 3.348 | 0.948 / 3.352 | 0.975 / 3.435 |
| 1 | D3D12 | 1.109 / 7.996 | 1.394 / 7.745 | 1.676 / 7.751 | 1.815 / 7.884 |
| 2 | D3D12 | 0.752 / 5.396 | 0.934 / 5.167 | 1.143 / 5.177 | 1.242 / 5.261 |

After (multi-draw per surface where `SUPPORTS_MULTI_DRAW_INDIRECT`):

| View | Backend | source CPU/GPU | culling | +CLOD | +HZB |
| --- | --- | --- | --- | --- | --- |
| 0 | Vulkan | 0.810 / 3.678 | 0.896 / 3.407 | 0.924 / 3.433 | 0.960 / 3.468 |
| 1 | Vulkan | 1.141 / 7.950 | 1.408 / 7.779 | 1.442 / 7.737 | 1.541 / 7.863 |
| 2 | Vulkan | 0.872 / 5.201 | 1.072 / 4.846 | 1.134 / 4.846 | 1.165 / 4.949 |
| 0 | D3D12 | 0.673 / 3.600 | 0.737 / 3.351 | 0.761 / 3.343 | 0.830 / 3.423 |
| 1 | D3D12 | 1.131 / 7.964 | 1.414 / 7.763 | 1.442 / 7.759 | 1.493 / 7.826 |
| 2 | D3D12 | 0.792 / 5.401 | 0.879 / 5.173 | 0.919 / 5.173 | 1.048 / 5.255 |

Findings, from one session per build (source mode reproduces within about
0.04 ms CPU / 0.15 ms GPU across the two builds, which bounds the noise):

- Multi-draw removes most CLOD submission cost: CLOD over culling fell from
  +0.17–0.31 ms to +0.02–0.06 ms CPU. GPU time is unchanged. CLOD pixels stay
  byte-identical (dense-forest A/B at 0 px error).
- GPU MultiMesh culling saves 0.17–0.36 ms GPU but costs 0.06–0.29 ms CPU, from
  three dispatches and uniform-set lookups per eligible surface (about 700
  patches here). Batching that work is the next CPU target.
- Current-frame HZB costs 0.03–0.13 ms GPU and 0.03–0.13 ms CPU and saves
  nothing here: rocks and terrain are seldom hidden, and HZB only rejects CLOD
  opaque commands after the full depth prepass. It stays opt-in.
- Viewport render info `DRAW_CALLS_IN_FRAME` counts CLOD group records, not API
  calls, so it is unchanged by multi-draw.

### Follow-up: skip culling for fully visible MultiMeshes

Same benchmark after skipping GPU culling for MultiMeshes that lie inside every
frustum plane. The bench now prints how many visible patches are fully inside
(same plane test as the renderer, out-of-range patches excluded): view 0 has 0
of 46, view 1 has 112 of 172, view 2 has 25 of 90.

Culling-only CPU overhead over source, p50 ms (before → two runs after):

| View | Vulkan | D3D12 |
| --- | --- | --- |
| 0 (0 fully inside) | +0.09 → +0.12, +0.10 | +0.06 → +0.18 |
| 1 (112 fully inside) | +0.27 → +0.18, +0.13 | +0.28 → +0.11 |
| 2 (25 fully inside) | +0.18 → +0.14, +0.09 | +0.09 → +0.08 |

View 0 cannot change and shows the single-run noise (up to about 0.1 ms).
The saving appears where many patches are fully inside. GPU culling savings stay
in the 0.2–0.5 ms range. The dense-forest A/B test gained a fully visible
overview view: pixels stay byte-identical, the culling stage does not dispatch
there and CLOD still does.

### GPU geometry counters in the valley benchmark

With `viewport_get_gpu_geometry_statistics` the bench prints per-view counters
(culling + CLOD + HZB, CLOD error 1 px, Vulkan):

| View | MultiMesh visible / tested (+ fully visible) | CLOD groups visible / tested | CLOD triangles | HZB rejected |
| --- | --- | --- | --- | --- |
| 0 | 3203 / 6286 (+0) | 278 / 2351 | 17,119 | 30 groups, 1,416 tris |
| 1 | 3823 / 8036 (+8774) | 1435 / 3473 | 94,823 | 351 groups, 23,915 tris |
| 2 | 4839 / 8824 (+1150) | 818 / 2793 | 50,168 | 215 groups, 12,360 tris |

The viewport submits 1.3–6.5 M primitives per frame, so CLOD geometry is a
small share. Most triangles are full-detail MultiMesh trees (about 200–450 each)
and grass. HZB rejects up to a quarter of CLOD triangles, but only after the
complete depth prepass has drawn them, which is why it gains nothing here.
Per-instance LOD for MultiMesh vegetation is the larger opportunity.

## Vegetation index LODs and per-instance GPU LOD — 2026-11-06

The valley now generates index LODs for its vegetation meshes by default
(`--no-mesh-lods` turns them off). Godot's CPU LOD alone lowered view 1 source
GPU time from 7.80 ms to 6.78 ms, but patches at a reduced LOD then kept direct
draws (culled surfaces in view 1: 60 to 39).

Per-instance GPU LOD in the culling kernel (D010). A first version bucketed all
LODs as 0, L/3, 2L/3, L and regressed (view 1, Vulkan: culling GPU 7.23 ms
versus source 6.94 ms, CPU 1.55 ms versus 1.14 ms). The kept version spans the
LODs between the bounds' nearest and farthest points.

Culling (with per-instance LOD) minus source, p50 ms, valley, RTX 3060:

| View | Vulkan run 1 CPU / GPU | Vulkan run 2 CPU / GPU | D3D12 CPU / GPU |
| --- | --- | --- | --- |
| 0 | +0.11 / -0.27 | +0.12 / -0.28 | +0.07 / -0.22 |
| 1 | +0.14 / -0.26 | +0.12 / -0.36 | +0.09 / -0.23 |
| 2 | +0.09 / -0.25 | +0.15 / -0.30 | +0.25 / -0.21 |

Before this slice (CPU LOD, reduced-LOD patches not culled, one Vulkan run):
+0.09 / -0.19, +0.03 / -0.18 and -0.01 / -0.40. In absolute terms the culling
mode's GPU time is unchanged within noise (view 1: 6.60 ms before, 6.65 and
6.55 ms after). Render-thread CPU grew by about 0.05-0.1 ms in views 1 and 2,
because more surfaces dispatch (view 1: 91 instead of 39). The 48 m patches
already give each patch a fitting LOD, so per-instance levels change few
instances (view 1: 1,135 of 6,331 culled-path instances above level 0).

Where one MultiMesh spans a large distance range, the gain is large. The
`multimesh_lod` fixture (479 spheres of 9k triangles, 10 LODs, all in view)
times 120 frames each way: GPU p50 1.237 ms source versus 0.420 ms with GPU LOD
on Vulkan, 1.313 ms versus 0.462 ms on D3D12. It also asserts exact per-level
counts against the CPU rule (two MultiMeshes, one spanning LOD 0-10, one LOD
4-7), zero coverage mismatch against single-instance CPU LOD references, and
byte-identical pixels with LOD disabled.

## Shadow-pass culling, jittered views and kernel binding — 2026-11-06

Culling minus source, p50 ms, valley, RTX 3060 (one run each; noise about 0.1 ms):

| View | Vulkan, shadows off: CPU / GPU | Vulkan, shadows on: CPU / GPU | D3D12, shadows off: CPU / GPU |
| --- | --- | --- | --- |
| 0 | +0.06 / -0.27 | +0.15 / -0.30 | +0.03 / -0.22 |
| 1 | +0.05 / -0.05 | +0.20 / -0.35 | +0.04 / -0.22 |
| 2 | +0.19 / -0.26 | +0.25 / -0.28 | +0.03 / -0.20 |

Shadow passes cull 156/233/133 surfaces, but reject only 11-16% of tested
casters (view 1: 17,366 of 19,465 visible). The CPU cost is consistent; the GPU
difference is not (the Vulkan view 1 "shadows off" GPU value is an outlier: its
culling+CLOD row in the same run is -0.28). Shadow culling therefore ships off
by default (D011). With shadows off, the culling CPU overhead on D3D12 fell from
+0.07..+0.25 ms (previous run) to +0.03..+0.04 ms after shared buffers moved to a
once-per-mode descriptor set; Vulkan shows a smaller change. One run per
backend, so treat this as indicative.

Correctness: the statistics fixture adds a shadowed spot light with exact
shadow counts (300 tested, 200 visible); the dense-forest A/B now enables
shadow culling (byte-identical directional and local shadow pixels) and adds a
TAA capture: culled and source TAA images differ in 0 pixels (source repeat:
0), with culling dispatched and HZB skipped under jitter.

## Voxel far field and distance thinning — 2026-11-06

Valley GPU p50 ms, culling mode, RTX 3060. Far field: patches beyond 180 m
replaced by `VoxelProxyBuilder` splat proxies (`--far-field`). Vulkan ran
base, far field, then far field, base (logs `bench_batchbase*`, `bench_farfield*`):

| View | Vulkan base (2 runs) | Vulkan far field (2 runs) | D3D12 base | D3D12 far field |
| --- | --- | --- | --- | --- |
| 0 | 3.10 / 3.12 | 2.87 / 2.88 | 3.09 | 2.85 |
| 1 | 6.27 / 6.28 | 5.29 / 5.27 | 6.25 | 5.26 |
| 2 | 4.33 / 4.32 | 3.78 / 3.78 | 4.33 | 3.80 |

The far field saves 0.23, 1.0 and 0.54 ms (7%, 16%, 12%), repeatable within
0.04 ms across runs, orders and backends. Source mode shows the same savings,
so the gain comes from drawing fewer far triangles, not from culling. View 1
draws 198 instead of 257 commands. CPU p50 moved by up to 0.5 ms between
identical base runs on this machine, so no CPU claim is made.
Quality is checked by the voxel proxy fixture (grove coverage 0.0507 against
0.0476, mean linear colour within 0.08) and the valley screenshots; the valley
has no per-pixel far-field metric yet, and the 180 m switch is a hard cut.

Distance thinning (ground cover thinned between 30 and 220 m, `--thinning`,
one Vulkan run): drawn instances fall from 3203/6331/5584 to 2478/5955/4885,
tested instances rise in views 1 and 2 (10,544 to 11,755 and 9,569 to 9,701)
because thinned surfaces lose the fully-inside skip, and GPU p50 is unchanged
(3.05/6.24/4.29 against 3.10/6.27/4.33 in the first base run). The valley is not bound by ground-cover
vertices; see D014.

CLOD streaming has a correctness fixture only (exact convergence, 14 evictions
without holes under a pool between one view's 212,280 B and two views'
289,920 B working sets); no frame-time measurement yet.

## GPU grass and far-field cross-fade — 2026-11-06

Valley GPU p50 ms, culling mode, RTX 3060. GPU grass: `GrassField3D` with a
45 m radius, 0.5 m patches and 24 blades per patch at full density
(`--gpu-grass`). Vulkan ran base, grass, grass, base:

| View | Blades | Vulkan base (2 runs) | Vulkan grass (2 runs) | D3D12 base | D3D12 grass |
| --- | --- | --- | --- | --- | --- |
| 0 | 78,647 | 3.10 / 3.09 | 3.39 / 3.39 | 3.10 | 3.37 |
| 1 | 72,219 | 6.31 / 6.25 | 6.52 / 6.57 | 6.29 | 6.50 |
| 2 | 92,679 | 4.35 / 4.30 | 4.77 / 4.77 | 4.33 | 4.78 |

Grass costs 0.21-0.45 ms per view, about 3-5 ns per generated blade, including
generation and drawing. A reference engine measured +0.25 to +0.55 ms for 0.09-0.26 M
mesh-shader blades on its own paths; the scenes differ, so this is context, not
a comparison. CPU p50 is unchanged within noise.

Far field with the 8 m cross-fade against the earlier hard cut (Vulkan; fade,
hard cut, hard cut, fade): 2.90/2.88 against 2.89/2.90, 5.28/5.32 against
5.32/5.31 and 3.77/3.77 against 3.77/3.77 ms. The difference stays within
0.04 ms.

## GPU voxel levels against static proxies — 2026-11-06

Valley GPU p50 ms, culling mode, RTX 3060, Vulkan, far field on. The static
mode uses `VoxelProxyBuilder.build` proxies at 32 voxels per patch edge
(171,422 voxels). The GPU mode uses `VoxelField3D` at 64 voxels per patch
edge, 4 bricks and 2 px (2,308,891 voxels on all levels). Order: static,
GPU, GPU, static (logs `bench_farstatic*`, `bench_fargpu*`):

| View | Static (2 runs) | GPU levels (2 runs) | Difference |
| --- | --- | --- | --- |
| 0 | 2.90 / 2.93 | 2.86 / 2.86 | -0.05 |
| 1 | 5.40 / 5.51 | 6.04 / 6.28 | +0.71 |
| 2 | 3.81 / 3.78 | 3.85 / 4.06 | +0.16 |

These runs were noisier than the earlier far-field runs: p95 was 0.6-1.5 ms
above p50, against about 0.4 ms before. View 1 looks over most of the far
field, so it pays the most. The valley build takes 3.8 s instead of 1.9 s,
and the fields hold 47-74 MiB of GPU memory, depending on the view.

Image error against the source meshes (`--far-compare`, view at 27 m
height). The two numbers are the mean absolute linear difference x 1000,
then the share of pixels off by more than 0.05:

| Far field | Error | Pixels > 0.05 | GPU ms (this view) |
| --- | --- | --- | --- |
| Static proxies, 32 voxels | 5.13 | 3.47% | 5.76 |
| GPU levels, 64 voxels, 2 px | 3.69 | 2.51% | 6.01-6.39 |
| GPU levels, 128 voxels, 1.5 px | 3.43 | 2.25% | 8.88 |
| Source meshes | 0 | 0% | 6.36 |

Repeated source runs differ by exactly 0, so the metric has no noise floor
in this view. The GPU field at 64 voxels and 2 px is the valley's flying
default. At 128 voxels the extra quality is small and the cost is large.

**Correction, 2026-11-08.** These GPU numbers are the viewport's render time,
which starts at the viewport. The fields' selection runs before it
(`call_on_render_thread()`), so it was never counted. Whole-frame GPU time
(`devtools/test/gpu_intervals.ps1`, the flying default view, RTX 3060):

| Far field | Vulkan | D3D12 |
| --- | --- | --- |
| Static proxies (`--far-static`) | 5.55 | 5.77 |
| GPU levels, one timestamp per field (before) | 10.43-10.45 | |
| GPU levels, no per-field timestamp | 7.52-7.68 | 7.38 |

Each of the 149 fields captured a GPU timestamp, and a timestamp orders the
render graph around it, so every field's clear, selection and copy ran one
after another: 3.1 ms. Without it, the selection costs 0.5 ms. The rest of
the difference to the static proxies is drawing: the depth pre-pass +0.78
ms and the opaque pass +0.82 ms for about 27,000 instanced two-triangle
quads, against one merged mesh per patch.

## Repeatable performance protocol (future gates)

Stable-compaction follow-up: three dispatch stages, 64-aligned ID ranges,
32 MiB ID + 32 MiB rank + 0.5 MiB block-scratch cap per viewport, with metadata
and commands budgeted separately. No new production readback. Vulkan and D3D12
kernel/raster matrices pass; standard smoke rerun initially found the new class
document's self-closing section, corrected before commit; final standard smoke
rerun passes. The kernel checks 65, zero and 16641 input IDs (261 blocks) to cover
partial workgroups and carry propagation across multiple scan chunks.

- Pin engine revisions, dirty diff, GPU/driver/backend, resolution, material and
  shadow/GI settings, AA, projection, camera path/seed, instance distribution and
  actual visibility. Separate import/cook/startup time from steady state.
- Run matching uncull/cull conditions with the same image contribution. Include
  small cases, 100k, 1M when practical, high visibility, heavy occlusion, boundary
  frusta, camera cuts, fast translation, resize and multiple simultaneous views.
- Disable adaptive exposure, changing sun/weather, VSync/frame caps and movie
  capture for timing. Warm pipelines/residency before sampling; include cold
  streaming separately. Alternate A/B and repeat to establish noise/spread.
- Report CPU wall and render-thread time; GPU frame and pass p50/p95/p99;
  potential/tested/visible instances and clusters; emitted triangles and calls;
  complete memory accounting and streaming bytes/latency/residency/evictions.
- Read GPU telemetry asynchronously and only when enabled. Never add blocking
  visibility readback merely to obtain a benchmark number.
- Image correctness precedes speed: uncull reference, stable-ID/custom-data and
  motion-vector checks, material/deformation fallback, shadows outside camera,
  reflection captures, double precision, transparency, orthogonal/XR fallback.
- Force list capacity and memory pressure. Overflow/missing pages cannot create
  holes. Destroy/reallocate/reimport while old frames are in flight.

Only after matching geometry, materials, lighting, shadows, AA, distance cuts and
quality can reference-engine and Godot timing be compared. Historical reference records are
research inputs, not targets or rerun measurements.

## HZB occlusion city bench — 2026-11-07

`devtools/test/hzb_bench.ps1`, `misc/large_world_test_project/hzb_bench.tscn`.
1920x1080, RTX 3060, no shadows, VSync off. 8x8 blocks of buildings (plain
boxes), 225 CLOD spheres (66k source triangles each, 14.9 M in all) at street
crossings and mid-streets, 3744 MultiMesh street props (1.1k triangles each,
one GPU-culled MultiMesh per street). MultiMesh GPU culling and CLOD are on in
every mode. Modes: off; CLOD single pass; CLOD two passes (D023); two passes
plus MultiMesh occlusion (D024). Each mode 2 x 240 frames, alternating order
after a warm-up. GPU p50 ms from `viewport_get_measured_render_time_gpu`;
stages from always-on RD timestamps (`GPU Geometry Depth Pre-Pass`, `Mesh CLOD
HZB`, `Mesh CLOD Phase 2 Depth`, `MultiMesh HZB Occlusion`, `GPU Geometry
Opaque Pass`). One run per backend.

| View | Backend | off | single | two-pass | two-pass + MultiMesh |
| --- | --- | --- | --- | --- | --- |
| street | Vulkan | 5.20 | 5.11 | 5.43 | 4.93 |
| street | D3D12 | 5.02 | 5.10 | 5.32 | 4.76 |
| corner | Vulkan | 3.23 | 3.33 | 3.65 | 3.29 |
| corner | D3D12 | 3.15 | 3.21 | 3.44 | 3.09 |
| aerial | Vulkan | 6.42 | 6.51 | 6.81 | 6.41 |
| aerial | D3D12 | 6.31 | 6.40 | 6.63 | 6.20 |

Stages, Vulkan street (D3D12 within 0.15 ms; its phase 2 is 0.21):

| Mode | depth | of which phase 1 | HZB build + CLOD test | phase 2 | MultiMesh test | opaque |
| --- | --- | --- | --- | --- | --- | --- |
| off | 2.33 | 2.33 | - | - | - | 2.76 |
| single | 2.46 | 2.31 | 0.13 | - | - | 2.56 |
| two-pass | 2.76 | 2.34 | 0.16 | 0.28 | - | 2.56 |
| + MultiMesh | 2.76 | 2.32 | 0.16 | 0.28 | 0.03 | 1.98 |

Counters (street): CLOD selects 55 groups (227k triangles); 52 groups (214k
triangles) are occluded, and with two passes all of them are deferred from
phase 1, none recovered. MultiMesh: 3044 of 3096 frustum-visible instances
occluded (corner 1967 of 2041, aerial 2412 of 3744).

Findings:
- MultiMesh occlusion is a clear win in occluded views: opaque -0.58 (street),
  -0.37 (corner), -0.50 ms (aerial) on Vulkan for a 0.03 ms test; the frame is
  0.35-0.56 ms faster than two-pass alone on both backends.
- CLOD single pass costs 0.10-0.14 ms (HZB build and test) and saves about
  0.2 ms of opaque work only in the street view; elsewhere it is neutral to
  +0.1 ms. CLOD already reduces the far spheres to few triangles, so little
  shading is hidden.
- CLOD two passes is a net loss here (+0.2 to +0.3 ms against single):
  deferring 214k triangles does not make phase 1 cheaper (2.31 vs 2.34 ms;
  depth cost is dominated by buildings and props), and phase 2 costs a fixed
  0.28 ms (Vulkan) / 0.21 ms (D3D12) in every view, also when it recovers
  nothing. Not yet verified why; a likely cause is that phase 2 walks every
  CLOD group command (one per group of each hierarchy, mostly zero
  instances) for all 225 surfaces in a second depth draw list. Two-pass would
  pay off only where hidden CLOD triangles dominate depth cost; compacting
  phase-2 commands (or skipping surfaces with no deferred group) would be
  needed before it is a good default.
