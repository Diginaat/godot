# Testing and benchmark protocol

Every renderer milestone passes these gates before it is called done. The
detailed rules for the geometry work are in
[knowledge/PERFORMANCE_BENCHMARKS.md](../../knowledge/PERFORMANCE_BENCHMARKS.md)
("Repeatable performance protocol"); read [kb/mistakes/](../../kb/mistakes/index.md)
before measuring.

## Gates per milestone

1. **Build:** `python -m SCons -k platform=windows target=editor production=yes`
   (see [CUSTOM_BUILD.md](../../CUSTOM_BUILD.md)).
2. **Standard smoke:** `devtools\test\smoke.ps1` (PhysX GPU on D3D12, path
   tracer on Vulkan, DDGI).
3. **Large-world fixtures:** `devtools\test\large_world.ps1 -Driver vulkan` and
   `-Driver d3d12` (GPU culling, CLOD, HZB, statistics, lifetime).
4. **Correctness of the new path:** an exact pixel A/B against the CPU path
   (new path on/off, same frame), on Vulkan and D3D12, covering the cases
   below that the change touches. Add a fixture to `large_world.ps1`.
5. **Unit tests** (CPU-side structures): a separate tests build that keeps its
   own object files, so it never invalidates the production build:
   `python -m SCons -j10 platform=windows target=editor production=yes tests=yes object_prefix=t_ extra_suffix=tests`
   (full build the first time), then
   `bin\godot.windows.editor.x86_64.tests.console.exe --test --test-case="*GPUInstanceSlots*"`.
   Tests live in `tests/servers/rendering/` and register themselves.
6. **Performance:** the benches below, before and after, same session,
   alternating order where the bench supports it. Report regressions.

## Correctness cases

Static and moving objects; camera cut (teleport) and fast rotation; first
frame after a node is added; objects with changing AABB; near-plane
intersections; transparent and alpha-scissor materials; shadows from
casters outside the camera frustum (directional, spot, omni); motion vectors
(TAA, DLSS); two viewports with different cameras; editor viewport;
`PlanarReflector3D` mirror and reflection probes; resize; freeing an
instance while frames are in flight; double-precision builds (fallback);
DDGI and path tracer scenes (smoke).

## Benchmarks

| Bench | Command | Measures |
| --- | --- | --- |
| Many instances (CPU) | `devtools\test\instance_bench.ps1 -Driver vulkan [-RenderThread separate]` | Separate `MeshInstance3D` nodes, 1k to 100k; wall frame, render CPU, GPU p50/p95/p99; per-stage CPU from frame profile markers |
| Valley | `devtools\test\forest_bench.ps1 -Driver vulkan -Label <name>` | Real large-world scene: MultiMesh vegetation, CLOD, HZB, shadows |
| HZB city | `devtools\test\hzb_bench.ps1 -Driver vulkan` | Occlusion-heavy views |
| Whole-frame GPU | `devtools\test\gpu_intervals.ps1` | Every timestamp interval, including work before the viewport |

Conditions to report: commit, GPU and driver, backend, resolution, render
thread mode, settings changed from default, the bench's own `*_SCENE:` line.
Pin VSync off, fixed exposure and frame-counted motion (the benches do).
One run proves little: run twice and give the spread.

## Bench notes

- `instance_bench` profiles stages in a separate short run after the timed
  frames: frame profile markers add render-graph nodes and change ordering.
- Viewport render time misses GPU work recorded before the viewport;
  use `gpu_intervals.ps1` for features that do that.
- `DRAW_CALLS_IN_FRAME` counts render elements (and CLOD group records),
  not API calls.
