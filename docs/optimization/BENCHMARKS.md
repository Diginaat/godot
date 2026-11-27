# Engine performance: benchmarks

RTX 5070, Windows 11, Vulkan, `production=yes` editor build, 1280x720.

## O1: VRAM (2026-11-27)

`Performance.RENDER_VIDEO_MEM_USED`, MB.

1,000 box MeshInstance3D nodes and a shadowed sun (a scratch scene):

| Build | VRAM |
| --- | --- |
| Stock 4.7.2 | 68.8 |
| Fork, GPU-driven features off | 68.8 |
| Fork before (default settings) | 80.2 |
| Fork after OD1 and OD2 | 72.2 |

Level bench (`misc/stock_compare_project/level_bench.gd`, 300 frames, two
runs each, the same VRAM in both runs):

| Preset | Stock (stock-compare) | Before | After OD1 | After OD1 and OD2 | GPU p50, OD1 only / OD1 and OD2 (ms) |
| --- | --- | --- | --- | --- | --- |
| town | 167 | 201 | 200 | 192 | 0.706 / 0.687 |
| forest | 158 | 237 | 229 | 221 | 1.430 / 1.411 |
| city_extreme | 188 | 475 | 423 | 415 | 3.335 / 3.309 |

The "OD1 only" runs come from a control build with the HZB change taken
out.

### After an unload (OD9)

`instance_bench.gd --case=small_1k,massive_100k --table --gpu-instances
--occlusion` (one process; massive_100k twice, then small_1k again),
video / buffer MB:

| Run | Before | After OD9 (Vulkan) | After OD9 (D3D12) |
| --- | --- | --- | --- |
| small_1k, fresh | 72.2 / 15.0 | 70.2 / 15.0 | 70.7 / 15.0 |
| massive_100k, second run | 244.3 / 99.4 | 135.9 / 78.1 | 136.4 / 78.1 |
| small_1k after massive_100k | 300 / 84-104 | 83.2 / 25.4 | 83.8 / 25.4 |

GPU p50 unchanged: small_1k 0.166 ms, massive_100k 0.78-0.95 ms.

Level bench, `--frames=300`, VRAM MB, the build before OD9 and after it
run in the same session:

| Preset | Stock 4.7.2 | Before OD9 | After OD9 | GPU p50 before / after (ms) |
| --- | --- | --- | --- | --- |
| town | 167 | 192 | 186 | - / 0.712 |
| forest | 158 | 221 | 165 | 1.678 / 1.679 |
| city_extreme | 188 | 415 | 287 | 3.486 / 3.490 |

GPU p50 in this session is higher than in the OD1/OD2 table above for
both builds (clocks); the A/B is from one session.

### Instance data built in the scene shader (OD10)

Same session, builds alternated, two rounds (VRAM identical in both):

| Preset or case | VRAM before / after (MB) | GPU p50 before / after (ms) |
| --- | --- | --- |
| town | 186 / 175 | 0.716 / 0.706-0.758 |
| forest | 165 / 149 | 1.672 / 1.622 |
| city_extreme | 287 / 232 | 3.455 / 3.313 |
| small_1k (instance bench) | 70.2 / 69.9 | 0.167 / 0.166 |
| massive_100k | 131.8 / 107.0 | 0.948 / 0.903 |
| churn_100k | 160.4 / 135.7 | 0.872 / 0.823 |

Cold stall (`shader_bench.ps1 -Cold -Mv`): 44.9, 44.6 s before; 44.4,
45.2 s after. Stock 4.7.2: town 167, forest 158, city_extreme 188 MB.

### Previous transforms out of the instance table (OD12)

Builds alternated, two rounds (VRAM identical in both); GPU p50 ranges:

| Case | VRAM before / after (MB) | GPU p50 before / after (ms) |
| --- | --- | --- |
| town | 175 / 175 | 0.715-0.719 / 0.719-0.724 |
| forest | 149 / 146 | 1.636-1.652 / 1.644-1.646 |
| city_extreme | 232 / 222 | 3.375-3.379 / 3.369-3.426 |
| small_1k | 69.9 / 69.9 | 0.167 / 0.167 |
| dynamic_20k | 86.7 / 85.2 | 0.696-0.701 / 0.698-0.701 |
| massive_100k | 107.0 / 101.8 | 0.890-0.909 / 0.888-0.910 |
| churn_100k | 131.5 / 120.3 | 0.830-0.980 / 0.830-0.977 |

### No lightmap UV scale in the record (OD13)

Same method; OD12 against OD13:

| Case | VRAM (MB) | GPU p50 (ms) |
| --- | --- | --- |
| town | 175 / 174 | 0.721-0.723 / 0.724-0.784 |
| forest | 146 / 145 | 1.637-1.655 / 1.640-1.656 |
| city_extreme | 222 / 219 | 3.355-3.363 / 3.366-3.378 |
| massive_100k | 101.8 / 100.1 | 0.886-0.910 / 0.884-0.903 |
| churn_100k | 120.3 / 116.6 | 0.828-0.974 / 0.826-0.971 |

## Moving instance bounds (OD14, 2026-11-27)

`level_bench.gd --frames=600`, three rounds, builds alternated, medians:

| Preset | Frame before / after (ms) | Render CPU before / after (ms) |
| --- | --- | --- |
| town | 1.165 / 1.169 | 0.630 / 0.621 |
| city_extreme | 8.553 / 8.421 | 5.225 / 5.103 |

## O2: deep chains (OD11, 2026-11-27)

`scene_bench.ps1`, 3 rounds, builds alternated (stock: same machine, 2 rounds):

| Scenario | Stock 4.7.2 | Before | After |
| --- | --- | --- | --- |
| deep 10k | 0.547 | 0.465 | 0.432 |
| deep 100k | 18.15 | 18.67 | 15.09 |
| reparent 10k | 0.076 | 0.072 | - |
| reparent 100k | 1.398 | 1.328 | - |

wide, parents, sparse, heavy, script and native: unchanged within spread.

## O2 and O5: process loop (2026-11-27)

`devtools/test/scene_bench.gd`, headless, frame ms, medians of 3 rounds
with the builds alternated. Editor build `production=yes
debug_symbols=yes`. Stock: Godot 4.7.2 official, 2 rounds, same machine.

| Scenario | Stock 4.7.2 | Before | After OD3 | After OD3 and OD4 |
| --- | --- | --- | --- | --- |
| script 10k | 2.33 | 1.486 | 1.178 | 1.141 |
| script 100k | 44.4-45.6 | 36.1 | 33.1 | 23.6 |
| native 10k | 0.123-0.127 | 0.148 | 0.148 | 0.145 |
| native 100k | 4.16-4.20 | 2.71 | 2.70 | 2.53 |

## O4: cold shader stall (2026-11-27)

`devtools/test/shader_bench.ps1 -Cold -Mv` (120 materials), Vulkan. The
driver's disk caches were emptied before every run.

| Build | Ubershader driver CPU (s) | Specialized (s) | Add-frame stall (s) |
| --- | --- | --- | --- |
| Before | 548.5, 544.7, 543.9 | 68.7, 68.3, 67.8 | 48.8, 48.3, 48.4 |
| OD5 | 529.9, 528.2, 532.9 | 68.1, 68.2, 68.1 | 47.4, 47.2, 47.5 |
| OD5 and OD6 | 495.9, 497.4 | 67.7, 68.2 | 44.4, 44.3 |

## O3: small_1k GPU time per pass (2026-11-27)

`gpu_intervals.gd` (`--gpu-profile`) on `instance_bench.gd --case=small_1k`,
static view, frames 150-350, Vulkan. Fork with `--table --gpu-instances
--occlusion` (its defaults).

| Build | Whole frame (ms) | HZB build (ms) |
| --- | --- | --- |
| Stock 4.7.2 | 0.142 | - |
| Fork before OD7 | 0.181 | 0.018 |
| Fork with OD7 | 0.173, 0.174 | 0.010 |

forest_map (default `gpu_intervals.gd` scene): HZB 0.033 to 0.021 ms,
frame 1.490 to 1.462 ms.

## O5: native property access (2026-11-27)

`scene_bench.ps1`, 3 alternated rounds, median frame ms (before / after
OD8): script 10k 1.132 / 1.075, script 100k 23.59 / 22.13, native 10k
0.147 / 0.147, native 100k 2.53 / 2.50; sparse and mixed within spread.
