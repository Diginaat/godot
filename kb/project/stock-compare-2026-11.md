---
type: Finding
title: Stock Godot 4.7.2 vs the fork (0.10.3 source), 2026-11-27
description: Game levels 2.9-6.0x faster, many-instance cases 1.1-15.9x, scene runtime up to 19x at 100k nodes, PhysX GPU 2.6-2.7x over Jolt from 8k bodies but 2.1x slower at 2k; cold shader stall 10% longer, more VRAM.
tags: [benchmark, stock-compare, renderer, scene-runtime, physx]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
sources:
  - id: doc
    resource: ../../docs/benchmarks/stock-compare.md
    title: Stock compare results and conditions
---

# Summary

Full tables and conditions: [stock-compare.md](../../docs/benchmarks/stock-compare.md).[^doc]
RTX 5070, Vulkan, 1280x720, fork `8d1f37b2e8` with defaults.

* Game levels (separate MeshInstance3D, shadowed sun, SSAO, glow): town
  15k nodes 3.55 to 1.24 ms (2.9x), forest 60k 7.26 to 1.65 ms (4.4x),
  city 200k 53.6 to 8.94 ms (6.0x). Render CPU is where it comes from
  (42.9 to 5.45 ms in the city); GPU time is also lower (6.8 to 3.8 ms).
* Many-instance cases: 1.1x (1k nodes) to 15.9x (10k unique materials).
* Scene runtime at 100k nodes: instancing 19x, churn 16x, wide/parents
  9-11x; deep, script, reparent 1.0-1.2x.
* Physics (ms per tick): PhysX GPU beats stock Jolt from 8k bodies (12.6
  to 4.9, 30k: 67.8 to 24.7) but is 2.1x slower at 2k boxes (fixed GPU
  cost about 1.7 ms per tick); PhysX CPU wins at 2k (0.73 vs 0.98).
* Worse than stock: cold 120-shader stall about 10% longer (two runs
  each), VRAM +35 to +285 MB, GPU time in a 1k-node scene 0.14 to 0.21 ms,
  scene_bench native at 10k +0.03 ms.

# Caveats

* PhysX keeps settled bodies awake far more than Jolt (crates_2k: 242 vs
  2,000 asleep): settled-scene cost differs by engine defaults.
* Stock 4.7.2 crashes on `--headless --import` of this project; the
  runner doesn't import (the scenes are built in code).
