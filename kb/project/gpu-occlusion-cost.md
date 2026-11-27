---
type: Finding
title: What GPU-path occlusion costs when nothing is hidden, and what it saves in game levels
description: With rendering/geometry/gpu_instances/occlusion and occlusion_two_pass on (the defaults), open instance_bench cases pay 0.02-0.03 ms GPU and town and city_extreme pay 0.04-0.3 ms of render CPU, while forest and city_extreme save 0.9 and 3.4 ms of GPU time. The defaults stay. The CPU cost is mostly recording the second depth pass in the render graph.
tags: [geometry, occlusion, hzb, gpu-instances, cpu, measuring]
status: stable
sources:
  - id: levels
    title: level_bench.gd --frames=600, town, forest, city_extreme, settings toggled with override.cfg, three alternated rounds, Vulkan, RTX 5070, 2026-11-27, branch dev-renderer-next at 338ea26f32
  - id: instance
    title: instance_bench.gd --stages with --gpu-profile and GPU timestamps (a local copy of the script), shared_20k, lights_20k, churn_100k, small_1k, two rounds, same day
  - id: profile
    title: devtools/profile/profile.ps1 on town and city_extreme, occlusion or two-pass on and off, same day
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

The stock comparison showed the fork's GPU time above stock in some
instance bench cases (shared_20k 0.631 / 0.698 ms, churn_100k 0.694 /
0.806 ms). Those scenes are open fields where nothing is hidden.

# Measured

Level bench, medians of three rounds (render CPU and GPU, ms):

| Preset | Defaults | `gpu_instances/occlusion` off | `occlusion_two_pass` off |
| --- | --- | --- | --- |
| town | 0.62 / 0.71 | 0.55 / 0.83 | 0.57 / 0.75 |
| forest | 0.59 / 1.55 | 0.65 / 2.43 | 0.65 / 1.90 |
| city_extreme | 5.19 / 3.46 | 4.93 / 6.83 | 4.93 / 4.86 |

Instance bench, normal timing, one run each: shared_20k GPU 0.704 with
occlusion and 0.682 without, churn_100k 0.823 and 0.791, the same
objects drawn. Per pass (GPU timestamps, two rounds): shared_20k's
opaque pass 0.314 with occlusion and 0.268 without; the HZB build and
test add 0.016; small_1k adds 0.014 in total.

With the timestamp markers on, churn_100k's opaque pass was faster with
occlusion (0.32 against 0.47 ms), the opposite of the normal run: the
markers change the render graph's order. Compare GPU passes only between
runs that both have markers, and check totals without them.

# Where the CPU goes

Profiles with two-pass on and off: the extra render CPU is in
`RenderingDeviceGraph::_run_render_commands` and
`_run_draw_list_command` (the second depth pass records every indirect
draw again) and in `_gpu_instances_begin_view`. Halving that would need
fewer indirect draws per pass (merged geometry), not a local change.

The per-bucket loop in `_gpu_instances_begin_view` takes 9 us in town
(586 buckets) and 78 us in city_extreme (1,841). Reading the mesh
compression values once per bucket instead of per frame saved only
8 us; most of the rest is `TextureStreaming::feedback_buffer_material_index()`,
which has to run every frame (it registers the material in the current
feedback buffer). Dropped.

# Decision

Keep both on by default: the GPU savings in levels with something to
hide (0.9 to 3.4 ms) are far larger than the cost in open scenes (0.02
to 0.03 ms GPU, up to 0.3 ms render CPU in city_extreme, which is CPU
bound in this bench).
