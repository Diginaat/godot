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

### Baseline (Phase 0)

Not measured yet: the first run failed to parse (frame-profile calls are
not exposed to GDScript) and had no hard stop. Fixed (RD timestamps,
`--quit-after`); the run is pending. Fill in from
`devtools/out/instance_bench/` with commit, GPU and driver.
