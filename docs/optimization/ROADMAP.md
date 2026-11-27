# Engine performance: roadmap

The second autonomous brief, after the renderer roadmap
([docs/renderer/roadmap.md](../renderer/roadmap.md)). It targets the places
where 0.10.3 is not much faster than stock Godot 4.7.2, measured in
[docs/benchmarks/stock-compare.md](../benchmarks/stock-compare.md).
Each row has a status. The numbers are in [BENCHMARKS.md](BENCHMARKS.md),
the log in [PROGRESS.md](PROGRESS.md).

| Id | Item | Baseline (stock / 0.10.3) | Status |
| --- | --- | --- | --- |
| O1 | VRAM of the GPU-driven systems: small projects must not pay for them, large ones should pay less | +35 to +285 MB in the level bench; +11 MB (the HZB) in a 1k-node scene | Started 2026-11-27: buffer growth with 1/8 headroom instead of powers of two, HZB at half resolution. City 475 to 415 MB, small scene 80 to 72 MB (stock 69). Open: per-member instance data (55 MB at 200k nodes), shrinking after a level is unloaded |
| O2 | Scene runtime where the fork gains little: reparent 1.0x, deep tree 1.1x, script processing 1.2x, native processing 1.3x, sparse transforms 1.4x (100k nodes) | `scene_bench.gd` | Started 2026-11-27: script processing (OD3, OD4) 36.1 to 23.6 ms at 100k, now 1.9x stock; native 10k still 0.02 ms behind stock (explained in part). Next: reparent, deep |
| O3 | Rendering of small scenes: small_1k 1.1x (GPU 0.14 to 0.21 ms), unique_lod_2k 1.7x | `instance_bench.gd` | Planned |
| O4 | Cold shader compilation about 10% slower than stock | `shader_bench.gd`: 32.4 / 36.5 s | Planned |
| O5 | GDScript VM: call and property access costs in `_process` | `scene_bench.gd` script | Notification allocation removed (OD3). Open: native property access with validated calls (about 13% of script 100k) |
| O6 | Threading: work that stays on the main or render thread | Profiles of O2 and O3 | Planned |

Rules:

- Measure first, and A/B against a control build of the same source.
- Never change the image or behavior silently.
- Keep every fallback.
- Record failed attempts in [REGRESSIONS.md](REGRESSIONS.md) and the KB.
