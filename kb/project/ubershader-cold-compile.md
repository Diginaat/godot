---
type: Finding
title: Fork code in the ubershader made the cold shader stall 8% longer
description: The cold stall of the shader bench is bound by the driver's work on ubershader pipelines. Two fork additions were compiled into every ubershader pipeline although the ubershader cannot fold them away - reading every instance field from either the CPU or the GPU path's buffer, and DDGI sampling for reflection probe captures. Loading the instance once per stage and making the DDGI capture bit a pipeline constant of capture ubershaders cut the ubershader driver CPU by 9% and the stall by 8.5%, with the same images.
tags: [shaders, pipelines, ubershader, compile, vulkan, ddgi, gpu-instances]
status: stable
sources:
  - id: bench
    title: devtools/test/shader_bench.ps1 -Cold -Mv (120 materials), Vulkan, RTX 5070, 2026-11-27, branch dev-renderer-next
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

The stock comparison had the cold shader stall about 10% longer than
stock 4.7.2. The stall waits for the ubershader pipelines:

- 329 ubershader pipelines took 545 s of driver CPU, 1.66 s each.
- 379 specialized pipelines took 68 s, 0.18 s each.

In the ubershader the specialization constants are read from the draw's
push constants, so the driver compiles every branch they guard. Two fork
additions grew every ubershader pipeline:

- `SCENE_INSTANCE(field)` read each instance field from `gpu_instances`
  or `instances` (57 uses, two loads each). A temporary ubershader that
  read `instances` only cut the ubershader CPU by 3.5%.
- `sc_ddgi_probe_capture()` guards DDGI irradiance sampling that only
  reflection probe captures under DDGI use. A temporary ubershader without
  it cut the ubershader CPU by a further 6%.

# Changes

- In the ubershader, each stage loads the drawn instance once
  (`scene_instance_load()`, after the vertex shader's GPU-path index
  lookup and at the start of `fragment_shader()`), and `SCENE_INSTANCE()`
  reads that copy. Specialized pipelines keep the old macro, which their
  constant folds.
- `sc_ddgi_probe_capture()` reads pipeline constant 1 in the ubershader
  (`pso_ubershader_packed_1`). Ubershader pipeline keys of color passes
  keep that one bit, so capture draws get their own ubershader pipelines.
- `GlobalPipelineData::use_ddgi_probe_capture` precompiles those for the
  reflection probe format. It is set as soon as a view's environment uses
  DDGI with `probe_capture_ddgi` on (not only at the first capture), so
  they compile in the background like the others.

# Results

Cold runs, `-Mv`, 120 materials:

| Build | Ubershader driver CPU (s) | Add-frame stall (s) |
| --- | --- | --- |
| Before | 548.5, 544.7, 543.9 | 48.8, 48.3, 48.4 |
| Instance loaded once | 529.9, 528.2, 532.9 | 47.4, 47.2, 47.5 |
| And DDGI capture bit | 495.9, 497.4 | 44.4, 44.3 |

Specialized pipelines: 68 s in every build. GPU p50 after the stall:
0.85-0.87 ms.

Images:

- Reflect view of the DDGI test project (DDGI, reflection probes, 240
  frames), before against after: within the run-to-run spread of one
  build (max 24/255).
- Every draw forced through the ubershader with a temporary switch,
  against specialized pipelines: 304 pixels more than 8/255 apart, max
  12. The same forced run with probe capture DDGI off: 101,691 pixels
  more than 8 apart, max 78. So the comparison would see a capture
  ubershader without DDGI.

# Open

- The rest of the gap to stock lies in other code the ubershader carries
  (DDGI GI-buffer blending, reflection normalization, planar reflections
  and material tables) or in the scheduling itself. The specialized-first
  proposal in [shader permutations](/project/shader-permutations.md) would
  remove most of the stall.
