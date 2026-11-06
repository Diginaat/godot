---
type: Mistake
title: Single-flag benchmark runs measured the defaults
description: A PowerShell harness splatted test arguments from a one-element array that had become a string; runs with one flag ran without it.
tags: [mistake, measuring, tooling, powershell]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-31T00:00:00Z }
---

# What happened

A benchmark loop built the arguments with
`$a = if ($v[1]) { $v[1].Split(' ') } else { @() }` and passed `@a`.
PowerShell unrolls a one-element array returned from an expression to a
plain string, and splatting a string passes nothing. Every run with a
single flag (`--planar=0`, `--probes=0`) measured the defaults; runs with
two flags worked. It showed as "planar off" still listing the planar pass
time and identical VRAM.

# Rule

Cast test arguments with `[string[]]$a = ...` before splatting, and check
each run's output for a sign that the flag took effect (a pass time that
should vanish, a changed setting in the BENCH line) before comparing
numbers.

# Again, through `powershell -File` (2026-11-06)

A far-field bench loop called `& powershell -File forest_bench.ps1
-ExtraArgs $flags` with a two-element array. A child `powershell -File`
receives plain strings, so `-ExtraArgs` got only the first flag
(`--far-field`) and `--far-gpu` was dropped. Four "static against GPU" runs
all measured the static proxies. The build line gave it away: the same
voxel count (171,422) in both modes, where the GPU field has 2,308,891.

Call a script that takes an array in-process (`& $script -ExtraArgs
$flags`), not through a child `powershell -File`, and compare a
mode-specific count in each log before the timings.
