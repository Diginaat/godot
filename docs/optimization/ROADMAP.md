# Engine performance: roadmap

The second autonomous brief, after the renderer roadmap
([docs/renderer/roadmap.md](../renderer/roadmap.md)). It targets the places
where 0.10.3 is not much faster than stock Godot 4.7.2, measured in
[docs/benchmarks/stock-compare.md](../benchmarks/stock-compare.md).
Each row has a status. The numbers are in [BENCHMARKS.md](BENCHMARKS.md),
the log in [PROGRESS.md](PROGRESS.md).

| Id | Item | Baseline (stock / 0.10.3) | Status |
| --- | --- | --- | --- |
| O1 | VRAM of the GPU-driven systems: small projects must not pay for them, large ones should pay less | +35 to +285 MB in the level bench; +11 MB (the HZB) in a 1k-node scene | Started 2026-11-27: buffer growth with 1/8 headroom instead of powers of two, HZB at half resolution. City 475 to 415 MB, small scene 80 to 72 MB (stock 69). OD9: buffers, the staging ring and transfer worker staging shrink (small scene after a 100k one 300 to 83 MB; city 415 to 287 MB, forest 221 to 165 MB). OD10: no per-member instance data (city 287 to 232 MB, forest 149 MB against stock 158). OD12, OD13: previous transforms only for instances that moved, no lightmap UV scale (record 160 to 96 B; city 232 to 219 MB). Open: city still +31 MB (table, member and ID lists, HZB), 13 MB still left after an unload |
| O2 | Scene runtime where the fork gains little: reparent 1.0x, deep tree 1.1x, script processing 1.2x, native processing 1.3x, sparse transforms 1.4x (100k nodes) | `scene_bench.gd` | Started 2026-11-27: script processing (OD3, OD4) 36.1 to 23.6 ms at 100k, now 1.9x stock; native 10k still 0.02 ms behind stock (explained in part). Deep: OD11, 100k 18.7 to 15.1 ms (1.2x stock); the dirty walk is what is left. Reparent 1.05x, per call already O(1) |
| O3 | Rendering of small scenes: small_1k 1.1x (GPU 0.14 to 0.21 ms), unique_lod_2k 1.7x | `instance_bench.gd` | Started 2026-11-27: GPU per pass against stock (gap 0.039 ms); HZB build four levels per dispatch (OD7), 0.018 to 0.010 ms. Left: the GPU paths' fixed culling cost (about 0.02 ms) |
| O4 | Cold shader compilation about 10% slower than stock | `shader_bench.gd`: 32.4 / 36.5 s | Done 2026-11-27 (OD5, OD6): ubershader driver CPU -9%, stall -8.5% (`-Mv`: 48.4 to 44.3 s). Not rerun against stock |
| O5 | GDScript VM: call and property access costs in `_process` | `scene_bench.gd` script | Notification allocation removed (OD3); native property access through validated calls (OD8): script 100k 23.6 to 22.1 ms. Open: compile-time property calls in GDScript (need a run-time guard for subclasses) |
| O6 | Threading: work that stays on the main or render thread | Profiles of O2 and O3 | Started 2026-11-27: city_extreme profiled (main thread: render 63%, moving-instance updates 23%, scripts 22%); OD14 moving-instance bounds without fmodf, frame 8.55 to 8.42 ms |

Rules:

- Measure first, and A/B against a control build of the same source.
- Never change the image or behavior silently.
- Keep every fallback.
- Record failed attempts in [REGRESSIONS.md](REGRESSIONS.md) and the KB.
