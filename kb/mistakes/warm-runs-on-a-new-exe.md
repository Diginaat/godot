---
type: Mistake
title: Warm A/B runs on a newly named exe measured the driver cache
description: A renamed but identical editor exe took 7x longer to create pipelines in "warm" runs, because NVIDIA's per-exe driver cache was never filled for it.
tags: [mistake, measuring, shaders, pipelines, vulkan]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-17T00:00:00Z }
---

# What happened

- The shader benchmark (`devtools/test/shader_bench.ps1`) compared a
  baseline exe with a new build copied as `godot_<variant>`. The new
  build's warm runs took 21.6 s of pipeline CPU instead of 2.7 s, and its
  steady frames were slower, which looked like a regression of the change.
- An identical copy of the baseline under a new name was just as slow, for
  five runs in a row. Both loaded the same 153 MiB Godot PSO cache.
- NVIDIA's driver cache (`%LOCALAPPDATA%\NVIDIA\GLCache`) is per
  executable and is filled when the driver compiles a pipeline itself. The
  baseline exe filled it in its cold run; a new exe that starts with a full
  Godot PSO cache never does.

# Rule

- Give every build a cold run (all caches cleared, see the benchmark)
  before its warm runs, and compare warm runs only after that.
- When a renamed exe is slower, rerun an identical copy under a new name
  before blaming the code.
