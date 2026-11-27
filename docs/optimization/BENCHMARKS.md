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
