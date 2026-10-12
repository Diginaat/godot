---
type: Technique
title: DDGI test protocol
description: How to measure a DDGI change so the result means something - converged comparisons, step response, motion error, flicker spread, cost, a real game level, and A/B builds.
tags: [ddgi, testing, measuring, protocol]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
sources:
  - id: ddgi-md
    resource: ../../DDGI.md
    title: DDGI.md, test project and findings log
---

# Ground rules

- Measure the GI buffer (`--debug=1`), not the final image or the probe
  view, when the question is about DDGI itself. Other effects (AO, fog,
  exposure) change the final image.
- Use `--anim=0 --tod=0` so nothing moves except what the test moves.
- Run every number at least twice, flicker three times. Write down the
  spread, not one value. A difference smaller than the spread is no
  difference.
- Fail fast: the runner kills the run on `Parse Error`, `Failed to load
  script` or `SCRIPT ERROR`. A script error otherwise leaves a gray window
  that looks like a rendering bug.
- Keep one copy of the editor per variant in `bin/` (for example
  `godot_base.windows.editor.x86_64.exe` and the matching `.console.exe`).
  Then an A/B needs one build per variant, not one per test.

# Tests (DDGI test project, `misc/ddgi_test_project`)

| What | Command (user args) | Read |
| --- | --- | --- |
| Converged brightness | `--view=interior --debug=1 --frames=600 --shot=...` | Image mean; only compare at 600+ frames (see [the convergence finding](/ddgi/findings/measure-after-convergence.md)) |
| Response to a light change | `--view=interior --debug=1 --frames=400 --measure=300 --trace_mean=1 --sun_step=0.5` | Frames to 90% / 98% of the change, overshoot. Repeats to a few frames |
| Error left by camera motion | `--view=outdoor --debug=1 --move=1 --frames=300 --settle=300 --shot=...` | Mean absolute difference between the moving shot and the `_settled` shot |
| Response to a moving light | `--view=interior --debug=1 --sun_step=0 --emissive_step=1 --frames=400 --measure=600` | `EMISSIVE_STEP`: frames to 50 / 90 / 98% of the way, noise floor subtracted. Repeats within about 10 frames |
| Flicker | `--view=interior --frames=600 --measure=60` | `flicker_std` (stable) and `flicker_px` (has outliers: 0.01-0.19% on an unchanged build); 6 runs |
| Weather fade stepping | `--view=interior --debug=1 --frames=600 --weather=dir` | Per-frame 99.9th percentile of the pixel change: 1 level on the baseline. Higher means probes visibly step one by one |
| Cost | `--gpu-profile` engine arg, `--view=room --quality=1 --res=1920x1080 --frames=200 --bench=300` | `ddgi_ms` and per pass; varies about ±0.02 ms between runs |
| Baking | `--volume=... --vspacing=0.5 --bake=path` | `BAKE ... OK` |
| Bounce light on a wall, still | `--view=bleed --tod=0 --anim=0 --debug=1 --frames=300 --nf=0` and `--nf=1`, plus a final image | Red next to the ball on the wall and floor, no blob around the probes; open floor the same with and without near-field rays |
| Bounce light on a wall, animated | `--fixed-fps 60 --write-movie <dir>/f.png --quit-after 720`, user args `--view=bleed --tod=0 --hud=0 --debug=1` | Contact sheet of frames 400-700: the red follows the ball, no trail, no blotches |

Then `devtools/test/smoke.ps1` before committing.

# Startup is not reproducible

The first 100-200 frames are not the same between identical runs.
Hit groups compile asynchronously, so what the first probe rays see
changes. The interior GI mean at frame 20 was 10, 16 or 30 in three
identical runs. Don't measure convergence from startup. Measure the
response to a change made after convergence instead (`--sun_step`).

# A real game level

The DDGI test project is small and synthetic. Every DDGI change also runs
in a real first-person game level (a private project, not in this
repository). The setup: storm weather (dark, no lightning), a bright
emissive sphere circling 5 m in front of the player, the DDGI volume
switched to baked + realtime updates, auto exposure off, `--fixed-fps 60`.
The sphere moves for 300 frames and stops for 300. Read the GPU time, the
mean of the GI buffer while moving and after the stop, the frame-to-frame
jitter, and the screenshots.

- Turn auto exposure off. It adapts over seconds and hides how fast the
  indirect light follows.
- Remove physics objects in the test (the demo's physics cubes are
  removed in test mode): their simulation differs between runs.
- Use `--fixed-fps 60` so weather, light transitions and the moving object
  advance the same amount per frame in every run.
- Even then, single frames differ between runs (GI mean 23.7 vs 20.3 at
  the same sphere position): rain particles and the probe schedule vary.
  Compare averages over 300 frames and the spread over 3 runs, not traces.
  A settle-time threshold of 10% or 2% of a 1-3 level change measures the
  drift noise, not the response.

# Bisecting a GPU ordering problem

When a pass changes DDGI only through scheduling or synchronization, use
the render graph switches in `rendering_device.cpp`: `GODOT_RG_REORDER=0`
(no reordering) and `GODOT_RG_FULL_BARRIERS=1` (full barriers). If either
fixes it, the graph's derived dependencies are too weak. Narrow it down
with graph synchronization points between single passes. This is how
[the bounce light loss](/ddgi/findings/render-graph-bounce-loss.md) was
pinned to ordering.
