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
