# Renderer benchmarks

Baseline and per-milestone results for the GPU-first renderer work. Method:
[testing.md](testing.md). Earlier geometry results (valley, city, MultiMesh
culling, CLOD, HZB) are in
[knowledge/PERFORMANCE_BENCHMARKS.md](../../knowledge/PERFORMANCE_BENCHMARKS.md)
and are not repeated here.

## Many-instance bench (`instance_bench`)

Scene: separate `MeshInstance3D` nodes on a 2 m grid, one shadowed
directional light (4 splits, 200 m), plain colour background, linear tone
mapping, 1280x720, VSync off. Static view: from one edge across the grid.
Orbit: same distance, 0.4 degrees per frame. 90 warm-up + 300 timed frames
per view, two repeats (second in reverse case order).

| Case | Nodes | Meshes x materials | Moving / frame | Lights |
| --- | --- | --- | --- | --- |
| `small_1k` | 1,000 | 1 x 1 | 0 | sun |
| `shared_20k` | 20,000 | 4 x 4 | 0 | sun |
| `unique_10k` | 10,000 | 1 x one per node | 0 | sun |
| `massive_100k` | 100,000 | 1 x 1 | 0 | sun |
| `dynamic_20k` | 20,000 | 4 x 4 | 2,000 | sun |
| `lights_20k` | 20,000 | 4 x 4 | 0 | sun, 128 omni, 8 shadowed spots |

### Baseline (Phase 0), 2026-11-10

Conditions: `dev-gpu-renderer` at `7e2aaabefb` (engine = `4bc2a22774`),
editor production build, RTX 3060, NVIDIA driver 32.0.16.2101, 12 CPU
threads, 1280x720, VSync off, default project settings (far directional
cascades cached, shadow LOD scale 4). Logs: `devtools/out/instance_bench/`.
Static view; the orbit view is within 1 ms of it in every case. p50 in ms;
"r0 / r1" are the two repeats.

| Case | Vulkan frame | Vulkan render CPU r0 / r1 | Vulkan GPU | D3D12 render CPU r0 / r1 | D3D12 GPU | Vulkan, separate render thread: frame |
| --- | --- | --- | --- | --- | --- | --- |
| `small_1k` | 0.64 | 0.38 / 0.34 | 0.27 | 0.41 / 0.33 | 0.27 | 0.69 |
| `shared_20k` | 8.75 | 8.11 / 6.19 | 1.42 | 8.14 / 5.99 | 1.39 | 9.08 |
| `unique_10k` | 11.95 | 8.05 / 7.84 | 1.35 | 8.64 / 8.54 | 0.65 | 11.83 |
| `massive_100k` | 21.03 | 20.42 / 19.48 | 1.15 | 20.12 / 19.62 | 1.16 | 21.11 |
| `dynamic_20k` | 9.37 | 7.24 / 7.44 | 1.41 | 7.03 / 7.80 | 1.39 | 7.87 |
| `lights_20k` | 7.25 | 6.63 / 6.28 | 1.67 | 6.22 / 6.02 | 1.60 | 7.21 |

Frame p95 is about 10 ms above p50 in `massive_100k` (30-31 ms) and 2-4 ms
in the 20k cases on both backends.

Per-stage render-thread CPU, Vulkan, static view, from `-Stages` (each
marker until the next one; a stage holds everything until the next marker):

| Stage (marker) | What it covers | `small_1k` | `shared_20k` | `unique_10k` | `massive_100k` | `dynamic_20k` |
| --- | --- | --- | --- | --- | --- | --- |
| Prepare Render Frame | `scene->update`: dirty instances, re-pairing, BVH | <0.02 | 0.04 | 0.05 | 0.04 | 1.03 |
| Cull DirectionalLight, Split # | cascade setup, then the whole threaded instance cull and positional-light culling | 0.11 | 0.36 | 0.27 | 1.20 | 0.34 |
| Render 3D Scene | scene data, light buffers, render buffers until Forward+ starts | <0.02 | 0.21 | 0.13 | 1.20 | 0.22 |
| Setup 3D Scene | main-pass `_fill_render_list`, sort, `_fill_instance_data`, GPU geometry compute | 0.08 | 4.08 | 1.52 | 16.38 | 2.87 |
| Render Directional/SpotLight Shadows | shadow render lists (per pass), shadow draw recording | 0.09 | 3.37 | 1.85 | 2.46 | 2.29 |
| Render Depth Pre-Pass | depth draw recording | <0.02 | 0.07 | 0.82 | 0.05 | 0.05 |
| Render Opaque Pass | opaque draw recording | <0.02 | 0.06 | 3.46 | 0.04 | 0.04 |

### What the baseline shows

1. **Every non-trivial case is CPU-bound on the render thread**: render
   CPU is 5x (`lights_20k`) to 18x (`massive_100k`) the GPU time. The frame
   follows render CPU; a separate render thread does not help.
2. **Main-pass render-list preparation is the largest cost**: 16.4 of
   20.4 ms at 84k visible objects in only 16 draws, about 195 ns per
   visible object per frame, in a static scene where nothing changed. This
   is the per-frame rebuild that the GPU instance table (Phase 1) and GPU
   culling into persistent buckets (Phase 2) remove. D025 holds.
3. **Culling is cheap**: the threaded scan plus light culling is 1.2 ms at
   100k. Moving culling to the GPU is not a goal by itself; it is needed so
   the CPU stops building per-visible-instance lists.
4. **Shadow lists cost 1.9-3.4 ms** even with far cascades cached
   (Phase 1.7 and Phase 6).
5. **Draw recording matters only with many distinct materials**: 4.3 ms
   for 8.8k draws in `unique_10k` (about 0.4 us per draw); 0.1 ms
   elsewhere. Multi-draw needs shared pipeline and material state, so this
   is a Phase 3 question (bindless material data), not Phase 1.
6. **Moving instances**: 2,000 moved nodes cost 1.0 ms in `scene->update`
   (about 0.5 us each: BVH, re-pairing). Phase 1.3 must not add to it.
7. **Small scenes**: `small_1k` renders in 0.3-0.4 ms CPU; any new path must
   keep it there (regression guard).

### Render-list split (Phase 1.0), 2026-11-10

New `RENDER_TIMESTAMP` markers inside the main pass and each shadow pass
(`render_forward_clustered.cpp`; only recorded while timestamps are
captured). Vulkan, static view, `-Stages`, ms p50:

| Step | `shared_20k` | `unique_10k` | `massive_100k` | `dynamic_20k` | `lights_20k` |
| --- | --- | --- | --- | --- | --- |
| Main: fill render list | 1.01 | 0.47 | 5.61 | 0.79 | 0.87 |
| Main: sort | 0.81 | 0.34 | 3.23 | 0.81 | 0.78 |
| Main: fill instance data | 2.26 | 0.69 | 7.54 | 1.39 | 1.39 |
| Shadows: fill render lists (all passes) | 0.63 | 0.23 | 0.96 | 0.46 | 0.52 |
| Shadows: sort | 0.80 | 0.29 | 0.35 | 0.80 | 0.78 |
| Shadows: fill instance data | 1.71 | 0.31 | 1.14 | 1.14 | 1.17 |
| Shadow draw recording ("Shadow Render List Ready" until the next marker) | 0.09 | 0.98 | 0.05 | 0.06 | 0.07 |

The three main steps add up to the 16.4 ms "Setup 3D Scene" of the
baseline (100k). Per visible element: fill about 67 ns, sort about 38 ns,
instance data about 90 ns. `_fill_instance_data` transposes two transforms,
does three mesh-storage lookups, writes the 176-byte record into mapped
memory and detects repeats.

Expected effect, to be measured in 1.6 and 1.7: the instance table replaces
the record build and write with a 4-byte slot ID, so most of the instance
data step (7.5 + 1.1 ms at 100k) should go; fill and sort (about 10 ms main
and shadows at 100k) stay until Phase 2 moves eligible instances out of the
CPU render list.

### GPU instance table, uploads only (Phase 1.2), 2026-11-10

`instance_bench.ps1 -Table` against the same build without it, Vulkan,
static view, p50 ms (render CPU / GPU). Nothing reads the table yet, so this
is the pure cost of keeping it current.

| Case | Off | On |
| --- | --- | --- |
| `small_1k` | 0.38 / 0.26 | 0.39 / 0.27 |
| `shared_20k` | 8.17 / 1.42 | 8.40 / 1.42 |
| `massive_100k` | 20.57 / 1.15 | 20.66 / 1.15 |
| `dynamic_20k` | 7.39 / 1.41 (r1 7.87 / 1.48) | 8.37 / 4.32 (r1 9.07 / 4.39) |
| `lights_20k` | 6.81 / 1.67 | 6.68 / 1.66 |

Static scenes pay nothing (no dirty records). `dynamic_20k` moves 2,000
nodes spread over the grid (every 10th), so each frame has about 2,000
one-record ranges; one `buffer_update` each costs 2.9 ms of GPU time and
about 1 ms of render CPU. Task 1.3 replaces scattered ranges with one
packed upload and a compute scatter. Table memory: 128 bytes per slot,
rounded up to a power of two (100k nodes: 16 MiB).

### Packed upload and compute scatter (Phase 1.3), 2026-11-10

Up to 16 contiguous runs of dirty records are still written with one
`buffer_update` each; more are packed into one upload (records plus a slot
list) and written to their slots by `gpu_instance_scatter.glsl` (one
dispatch). Same bench, `-Table`, Vulkan, static view, p50 ms (render CPU / GPU):

| Case | Table off (1.2 run) | Table on, 1.2 (ranges) | Table on, 1.3 (scatter) |
| --- | --- | --- | --- |
| `small_1k` | 0.38 / 0.26 | 0.39 / 0.27 | 0.45 / 0.27 (r1 0.36 / 0.26) |
| `shared_20k` | 8.17 / 1.42 | 8.40 / 1.42 | 9.56 / 1.40 (r1 6.65 / 1.39) |
| `massive_100k` | 20.57 / 1.15 | 20.66 / 1.15 | 21.16 / 1.15 (r1 20.06 / 1.15) |
| `dynamic_20k` | 7.39 / 1.41 (r1 7.87 / 1.48) | 8.37 / 4.32 (r1 9.07 / 4.39) | 8.15 / 1.44 (r1 8.35 / 1.42) |
| `lights_20k` | 6.81 / 1.67 | 6.68 / 1.66 | 7.03 / 1.65 (r1 6.76 / 1.64) |

The GPU cost of keeping 2,000 scattered moved records current is gone
(1.43 vs 1.41 ms). About 0.5-0.8 ms of render CPU remains in
`dynamic_20k` with the table on, in both repeats (likely building and
packing the dirty records); other differences are within the repeat
spread (`shared_20k` and `small_1k` first repeats).

### Where `_fill_instance_data` spends its time (before 1.5), 2026-11-10

Temporary build switch (not committed) that removes parts of the
per-element work; images are wrong in modes 1-3, only CPU time counts.
`-Stages`, Vulkan, static view, "Fill Instance Data" (main pass / all
shadow passes), ms p50:

| Mode | Removed | `shared_20k` | `massive_100k` |
| --- | --- | --- | --- |
| 0 | nothing | 3.30 / 2.12 | 7.80 / 1.29 |
| 1 | the two transform transposes | 1.43 / 1.10 | 5.95 / 0.94 |
| 2 | plus the mesh-storage lookups | 1.80 / 1.37 | 5.93 / 0.97 |
| 3 | plus the 176-byte write (a 4-byte slot instead) | 1.32 / 1.10 | 5.32 / 0.84 |

At 100k the record write costs about 0.6 ms and the transposes about
1.9 ms; about 5.3 ms (63 ns per element) is the walk itself: each render
element points to its surface cache and its instance object, scattered in
memory. `shared_20k` varies by about 0.4 ms between runs (mode 2 above
mode 1). Conclusion (D027): replacing the record with a slot ID saves at
most about 2.5 of 20 ms; the CPU has to stop visiting eligible instances.

### GPU path for eligible instances (Phase 2.1-2.4), 2026-11-10

`instance_bench.ps1` with and without `-GpuInstances`, Vulkan, safe render
thread, same session, ms p50 (static / orbit view, two repeats where they
differ by more than 1 ms):

| Case | Render CPU, CPU path | Render CPU, GPU path | GPU, CPU path | GPU, GPU path | Draws |
| --- | --- | --- | --- | --- | --- |
| `small_1k` | 0.36-0.41 | 0.34-0.40 | 0.26 | 0.27-0.34 | 3 / 2 |
| `shared_20k` | 7.0-10.1 | 3.5-6.1 | 1.36-1.40 | 1.27-1.39 | 113 / 17 |
| `unique_10k` | 8.4-9.3 | 8.1-8.7 | 1.28-1.36 | 1.12-1.15 | 8,800 / 10,001 |
| `massive_100k` | 21.6-22.5 | 5.3-5.6 | 1.15 | 1.26-1.45 | 16 / 2 |
| `dynamic_20k` | 8.0-10.6 | 4.8-5.9 | 1.37-1.42 | 1.36-1.44 | 113 / 17 |
| `lights_20k` | 7.6-7.7 | 3.4-4.5 | 1.62-1.65 | 1.55-1.60 | 113 / 17 |

Wall frame time follows render CPU (`massive_100k` 22.6 to 6.1 ms). The
remaining render CPU at 100k is mostly the shadow passes, which still use
the CPU lists (Phase 6). `unique_10k` gains nothing: 10,000 materials make
10,000 buckets, one indirect draw each, and their per-view bucket data. GPU
cost at 100k is the cull and 176-byte writes of 100k instance surfaces
(+0.1-0.3 ms). Viewport render info counted no objects or primitives for
GPU-drawn instances then (now it does, a few frames late). Buffer memory grows by about 64 MiB at 100k (table,
members and per-view instance data at power-of-two capacity).

After index LODs (D029, visible-ID indirection for every GPU path draw),
same bench with `-GpuInstances`, Vulkan: `massive_100k` render CPU
4.9-5.1 ms, GPU 1.26-1.30 ms; `shared_20k` 3.2-4.4 ms; `small_1k`
0.31-0.38 ms. No regression (the bench meshes have no LODs).

`churn_100k` (new, 100,000 nodes, 100 change material every frame), render
CPU p50, Vulkan, 2026-11-10: CPU path 30.5-32.3 ms; GPU path with buckets
updated in place (D031) 6.1-6.8 ms (`massive_100k` 4.8-5.2 ms in the same
run). Before D031 each frame also paid a full bucket rebuild, measured at
7.6 ms for 100k members.

With the scene cull's visibility bits (D032), same bench, Vulkan, render CPU
p50: `massive_100k` 5.2-5.4 ms (5.0-5.2 before), `churn_100k` 6.6-6.8 ms,
`small_1k` 0.31-0.34 ms. Built on the render thread from the pointer list
instead, the bits cost +2.7 ms at 100k.

### Directional shadow casters on the GPU path (Phase 6.1), 2026-11-10

`instance_bench.ps1 -GpuInstances`, Vulkan, safe render thread, render CPU
p50 static view, two repeats (before / after D033): `small_1k` 0.31-0.34 /
0.25-0.26 ms, `shared_20k` 4.3-5.5 / 0.69-0.79 ms, `unique_10k` 8.1-8.4 /
7.1-7.7 ms (draws 10,001 to 8,776, shadow draws as on the CPU path),
`massive_100k` 5.2-5.4 / 2.75-2.85 ms, `dynamic_20k` 4.4-5.4 / 2.1-2.2 ms,
`lights_20k` 3.3-4.2 / 0.75 ms, `churn_100k` 6.6-6.8 / 3.6-3.8 ms. GPU time
unchanged (1.26 ms at 100k). Before the used-bucket filter `unique_10k` had
30,003 shadow draws and a 17-18 ms frame.

Motion age-out over moving instances only (2026-11-10): before, every
render visited each instance the camera listed for the GPU path (84k
pointer chases at 100k, about 1.1 ms). Now a list holds the instances
whose motion status is not NONE. Render CPU p50, Vulkan, GPU path:
`massive_100k` 2.8-3.2 to 1.68 ms (frame 2.1 ms, GPU 1.30 ms),
`shared_20k` 0.63 ms, `churn_100k` 2.28 ms, `dynamic_20k` 1.88 ms.
The CPU and RT list walks went too (every status change adds to the list):
GPU path `massive_100k` 1.51-1.59 ms, `shared_20k` 0.57 ms, `small_1k`
0.23-0.24 ms; CPU path `massive_100k` 19.3-19.7 ms (baseline 20.4-21),
`shared_20k` 6.3-7.0 ms.

D3D12, same protocol, render CPU p50 (CPU path / GPU path): `small_1k`
0.40-0.54 / 0.29-0.40, `shared_20k` 7.7-10.1 / 3.2-6.2, `unique_10k`
8.2-9.2 / 7.9-8.9, `massive_100k` 20.7-22.3 / 5.5-5.9, `dynamic_20k`
7.9-9.5 / 4.8-7.3, `lights_20k` 7.0-8.2 / 4.0-5.0. GPU p50 changes by
+0.13 ms at 100k and +0.07 ms in `unique_10k`, less elsewhere.

### Open items found by the baseline

- `unique_10k` frame time is 3.6-4 ms above render CPU on both backends
  (other cases about 0.6 ms). Not explained yet; likely main-thread work per
  material or per instance outside the render thread. Check before Phase 3.
- D3D12 reports 734-822 MiB video memory in `unique_10k` against 104 MiB on
  Vulkan (10k materials). Look at per-material uniform set or descriptor
  memory on D3D12.
- Repeat spread: `shared_20k` render CPU 8.1 vs 6.2 ms between repeats on both
  backends (the first repeat runs right after `small_1k`). Treat
  differences under about 2 ms in the 20k cases as noise until the bench
  pins the cause.
- Video memory reported by the bench only grows within one run (buffers
  grow and are kept), so per-case memory is an upper bound.

GPU path HZB occlusion (D034, 2026-11-10), instance bench without
occluders (the cost only), Vulkan, p50 GPU path / with occlusion: GPU
`small_1k` 0.29 / 0.37 ms, `shared_20k` 1.32-1.37 / 1.42-1.49 ms,
`massive_100k` 1.27-1.33 / 1.39-1.45 ms, `lights_20k` 1.61-1.66 /
1.69-1.74 ms; render CPU +0.03-0.07 ms. Same run, render CPU p50 GPU path:
`massive_100k` 1.43-1.83 ms, `shared_20k` 0.57-0.61 ms, `small_1k`
0.24-0.27 ms.
