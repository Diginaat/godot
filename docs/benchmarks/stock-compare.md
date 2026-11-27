# Stock Godot vs this fork

![Stock Godot 4.7.2 vs the fork, 2026-11-27](stock-compare.png)

Runner: `devtools/test/stock_compare.ps1 -Stock <stock console exe>`.
Scenes: `misc/stock_compare_project/` (level and physics benches, stock 4.7
API only), `misc/large_world_test_project/instance_bench.gd` and
`shader_bench.gd`, `devtools/test/scene_bench.gd`. Each engine runs its own
copy of the project; builds alternate every round and the order flips every
other round; one unrecorded rendering run per exe first. Only what the fork
changed is measured: scene runtime, GPU-driven geometry, shader variants,
PhysX.

## Run of 2026-11-27

| | |
| --- | --- |
| Stock | Godot 4.7.2-stable (official) |
| Fork | 4.8-beta custom build `8d1f37b2e8` (own version 0.10.3), `production=yes`, PhysX GPU, Blast, Flow |
| Machine | NVIDIA GeForce RTX 5070, 12 CPU threads, Windows 11, Vulkan, 1280x720, vsync off |
| Fork settings | defaults (GPU instances, instance table, HZB occlusion on); the instance bench passes `--table --gpu-instances --occlusion` because it sets them itself |
| Rounds | level, physics, scene: 3; instance, shader: 2 |

Raw results (every run) are in
[stock-compare-2026-11-27/](stock-compare-2026-11-27/): `rendering.csv`
(warmup, level, instance and shader suites), `physics.csv` and `scene.csv`
(written with the machine's locale, so decimals use commas).

Values are medians over rounds. Round-to-round spread: level 1-3%; physics
up to 6% (GodotPhysics3D) and 6-14% for Jolt in crates_2k, where a tick is
below 1 ms; scene runtime min-max ranges are in the runner output.

### Game-level rendering (`level_bench.gd`)

Shadowed sun (4 cascades), sky, SSAO, glow, camera flying through the level,
separate MeshInstance3D nodes.

| Preset | Content | Stock frame (ms) | Fork frame (ms) | Speed-up | Render CPU stock / fork | GPU stock / fork | Draws stock / fork | VRAM stock / fork (MB) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| town | 15k nodes, 60 meshes, 300 materials, 64 omni, 4 shadowed spots, 400 moving | 3.55 | 1.24 | 2.9x | 2.50 / 0.67 | 0.88 / 0.73 | 7,428 / 1,107 | 167 / 201 |
| forest | 60k nodes, 12 meshes, 24 materials, long shadows | 7.26 | 1.65 | 4.4x | 6.46 / 0.64 | 2.45 / 1.62 | 6,663 / 229 | 158 / 236 |
| city_extreme | 200k nodes, 120 meshes, 4,000 materials, 256 omni, 16 shadowed spots, 5,000 moving | 53.6 | 8.94 | 6.0x | 42.9 / 5.45 | 6.82 / 3.75 | 49,997 / 3,393 | 188 / 472 |

p99 frame: town 5.65 / 2.03, forest 15.3 / 4.32, city_extreme 68.8 / 11.0.

### Many-instance cases (`instance_bench.gd`, static view)

| Case | Stock (ms) | Fork (ms) | Speed-up | Render CPU stock / fork |
| --- | --- | --- | --- | --- |
| small_1k | 0.398 | 0.367 | 1.1x | 0.233 / 0.155 |
| unique_lod_2k | 2.34 | 1.36 | 1.7x | 1.61 / 0.66 |
| shared_20k | 2.72 | 0.82 | 3.3x | 2.49 / 0.31 |
| unique_10k (one material per node) | 6.86 | 0.43 | 15.9x | 5.19 / 0.23 |
| massive_100k | 9.69 | 0.98 | 9.9x | 9.28 / 0.68 |
| dynamic_20k (2,000 moving) | 4.09 | 1.31 | 3.1x | 3.03 / 0.69 |
| lights_20k (128 omni, 8 shadowed spots) | 2.81 | 0.91 | 3.1x | 2.58 / 0.34 |
| churn_100k (100 material changes per frame) | 12.7 | 1.82 | 7.0x | 12.0 / 1.27 |

The orbit views give the same ratios (1.1x to 15.6x).

### Physics (`physics_bench.gd`, headless, `--fixed-fps 60`, ms per tick)

| Preset | Stock Jolt | Stock GodotPhysics3D | Fork PhysX GPU | Fork PhysX CPU | Fork Jolt |
| --- | --- | --- | --- | --- | --- |
| crates_2k (boxes) | 0.98 | 5.30 | 2.07 | 0.73 | 0.62 |
| mixed_8k (boxes, spheres, capsules) | 12.6 | 58.3 | 4.87 | 5.93 | 10.9 |
| extreme_30k | 67.8 | 375 | 24.7 | 36.7 | 52.0 |

No body was lost through the floor in any run. Jolt's default
`max_bodies` (10,240) is too low for 30k bodies; the project raises the Jolt
limits.

### Scene runtime (`scene_bench.gd`, headless, ms per frame)

| Scenario | 10k stock / fork | 100k stock / fork | 100k speed-up |
| --- | --- | --- | --- |
| instance (scene instancing, nodes in a group) | 31.6 / 9.58 | 2,541 / 131 | 19.4x |
| churn (2% freed and added) | 1.07 / 0.31 | 92.9 / 5.76 | 16.1x |
| churn_read | 1.26 / 0.39 | 102.8 / 7.08 | 14.5x |
| wide | 0.106 / 0.039 | 2.76 / 0.29 | 9.4x |
| parents | 0.173 / 0.021 | 2.86 / 0.25 | 11.4x |
| heavy | 1.74 / 0.80 | 49.9 / 23.6 | 2.1x |
| sparse | 0.023 / 0.020 | 0.67 / 0.46 | 1.4x |
| mixed | 0.033 / 0.020 | 0.71 / 0.43 | 1.7x |
| native | 0.122 / 0.154 | 4.86 / 3.60 | 1.3x |
| script | 2.71 / 1.63 | 49.3 / 40.9 | 1.2x |
| deep | 0.55 / 0.48 | 25.5 / 23.6 | 1.1x |
| reparent | 0.082 / 0.079 | 1.46 / 1.41 | 1.0x |

### Cold shader compilation (`shader_bench.gd`, 120 material shaders in one frame)

| | Stock | Fork |
| --- | --- | --- |
| Stall of the spawn frame (ms), per run | 32,414 / 30,342 | 36,533 / 33,656 |
| Steady frame after (ms) | 0.79-0.85 | 0.82-0.83 |

## Run of 2026-11-27 on the development branch

The same runner and machine, later the same day, on `dev-renderer-next`
at `338ea26f32` (after 0.10.3: previous transforms only for moved
instances, no lightmap UV scale in the instance table, global transforms
of dirty chains without recursion, moving instance bounds without
`fmodf`). The chart
above is still the 0.10.3 run. Raw results:
[stock-compare-2026-11-27b/](stock-compare-2026-11-27b/) (one CSV per
suite, decimals with commas). Three rounds for level, physics and scene,
two for instance and shader.

| Level preset | Stock / fork frame (ms) | Speed-up | p99 stock / fork | Render CPU stock / fork | GPU stock / fork | VRAM stock / fork (MB) |
| --- | --- | --- | --- | --- | --- | --- |
| town | 3.43 / 1.17 | 2.9x | 5.42 / 1.74 | 2.43 / 0.62 | 0.87 / 0.72 | 167 / 174 |
| forest | 6.83 / 1.57 | 4.3x | 13.8 / 4.50 | 6.09 / 0.60 | 2.43 / 1.54 | 158 / 145 |
| city_extreme | 51.4 / 8.63 | 6.0x | 65.5 / 10.3 | 40.9 / 5.23 | 6.84 / 3.50 | 188 / 219 |

| Instance case (static view) | Stock / fork frame (ms) | Speed-up | Render CPU stock / fork | GPU stock / fork | VRAM stock / fork (MB) |
| --- | --- | --- | --- | --- | --- |
| small_1k | 0.401 / 0.350 | 1.1x | 0.233 / 0.147 | 0.139 / 0.167 | 117 / 91 |
| unique_lod_2k | 2.25 / 1.33 | 1.7x | 1.55 / 0.63 | 0.257 / 0.260 | 149 / 136 |
| shared_20k | 2.67 / 0.72 | 3.7x | 2.46 / 0.30 | 0.631 / 0.698 | 127 / 97 |
| unique_10k | 6.39 / 0.42 | 15.1x | 4.82 / 0.23 | 0.468 / 0.276 | 129 / 102 |
| massive_100k | 9.46 / 0.86 | 11.0x | 9.06 / 0.64 | 0.719 / 0.738 | 149 / 116 |
| dynamic_20k | 3.83 / 1.18 | 3.2x | 2.90 / 0.58 | 0.635 / 0.711 | 149 / 103 |
| lights_20k | 2.76 / 0.82 | 3.3x | 2.53 / 0.32 | 0.753 / 0.801 | 165 / 118 |
| churn_100k | 12.1 / 1.64 | 7.4x | 11.4 / 1.17 | 0.694 / 0.806 | 165 / 151 |

The instance bench runs its cases in one process, so its VRAM column is
the process total after each case, not the case alone.

| Scene runtime (ms per frame) | 10k stock / fork | 100k stock / fork | 100k speed-up |
| --- | --- | --- | --- |
| instance | 27.5 / 8.01 | 2,462 / 122 | 20.2x |
| churn | 0.99 / 0.29 | 89.8 / 5.16 | 17.4x |
| churn_read | 1.17 / 0.32 | 99.3 / 6.20 | 16.0x |
| parents | 0.107 / 0.021 | 2.16 / 0.25 | 8.7x |
| wide | 0.106 / 0.024 | 1.78 / 0.29 | 6.1x |
| script | 2.38 / 1.10 | 45.2 / 23.3 | 1.9x |
| heavy | 1.65 / 0.76 | 38.4 / 21.1 | 1.8x |
| native | 0.120 / 0.141 | 4.16 / 2.75 | 1.5x |
| deep | 0.539 / 0.430 | 24.8 / 16.7 | 1.5x |
| mixed | 0.031 / 0.020 | 0.62 / 0.47 | 1.3x |
| sparse | 0.023 / 0.017 | 0.61 / 0.46 | 1.3x |
| reparent | 0.079 / 0.074 | 1.39 / 1.28 | 1.1x |

| Physics (ms per tick) | Stock Jolt | Stock GodotPhysics3D | Fork PhysX GPU | Fork PhysX CPU | Fork Jolt |
| --- | --- | --- | --- | --- | --- |
| crates_2k | 0.95 | 4.87 | 1.95 | 0.66 | 0.63 |
| mixed_8k | 12.4 | 54.5 | 4.23 | 5.50 | 9.46 |
| extreme_30k | 64.8 | 345 | 23.7 | 36.4 | 50.2 |

No body was lost in any run.

Cold shader stall (120 materials, spawn frame): stock 30.0 and 31.2 s,
fork 30.8 and 31.3 s; steady frame 0.74-0.80 / 0.78 ms; VRAM 117 / 119 MB.

Against the 0.10.3 run: the fork now uses less video memory than stock in
every instance case (it used up to 144 MB more), forest 236 to 145 MB and
city_extreme 472 to 219 MB (loading buffers freed after use, OD9, and a
smaller instance table, OD10 to OD13, in
[DECISIONS.md](../optimization/DECISIONS.md)). The scene runtime's deep and script scenarios are
1.5x and 1.9x stock (were 1.1x and 1.2x). The cold shader stall is now
within 3% of stock (was about 10% longer).

## Where the fork is not better

- **Physics with few bodies on the GPU:** PhysX GPU has a fixed cost per
  tick (about 1.7 ms while settled in crates_2k), so 2k boxes are 2.1x
  slower than stock Jolt. PhysX CPU (0.73 ms) is faster than stock Jolt
  there. PhysX GPU wins from about 8k bodies on (2.6x, 30k: 2.7x).
- **Sleeping:** Jolt sleeps almost every crate in crates_2k (2,000 of
  2,000); PhysX keeps most awake (242 asleep). Settled scenes cost more on
  PhysX.
- **Cold shader stall:** about 10% longer than stock (two runs each). The
  fork's 0.10.2 permutation work was measured against the older fork, not
  against stock.
- **VRAM:** in the 0.10.3 run +35 to +285 MB (instance table, HZB, GPU
  culling buffers; +144 MB already in the 1k-node case). On the
  development branch: town +7 MB and city_extreme +31 MB; forest and the
  instance cases use less than stock.
- **GPU time in tiny scenes:** small_1k GPU 0.14 to 0.21 ms (the frame is
  still faster through the CPU side); 0.139 to 0.167 ms on the development
  branch. The GPU paths' culling passes also cost shared_20k, dynamic_20k,
  lights_20k and churn_100k 0.05-0.11 ms of GPU time.
- **scene_bench native at 10k:** 0.122 to 0.154 ms (+0.03 ms); 100k is faster.

Fork Jolt is faster than stock Jolt in the 8k and 30k cases (10.9 vs 12.6,
52.0 vs 67.8 ms); not investigated (the 4.8 base, compiler or Jolt version).
