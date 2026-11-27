---
type: Finding
title: Moving instances paid six CRT fmodf calls each for their BVH bounds
description: RendererSceneCull::_update_instance() quantizes a moving instance's BVH bounds with AABB::quantize(), which calls Math::fposmodp() (fmodf) six times. The MSVC CRT fmodf is a slow exact-remainder loop; in city_extreme (5,000 moving instances) it was 15% of _update_instance. The unit there is always a power of two, where x - floor(x / unit) * unit gives the same value exactly. city_extreme frame 8.55 to 8.42 ms, render CPU 5.23 to 5.10 ms.
tags: [scene-cull, cpu, moving-instances, large-worlds]
status: stable
sources:
  - id: profile
    title: devtools/profile/profile.ps1 on level_bench.tscn --preset=city_extreme, debug-symbol production editor, 2026-11-27, branch dev-renderer-next
  - id: bench
    title: level_bench.gd --frames=600, town and city_extreme, builds alternated, three rounds, Vulkan, RTX 5070, same day
  - id: check
    title: numpy float32 comparison of both formulas, 259 million values (units 2^-16 to 2^19, random, exact multiples and negated), 0 mismatches
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

Main thread profile of city_extreme (200k nodes, 5,000 moving, 256 omni
and 16 shadowed spot lights): `RendererSceneCull::update_dirty_instances`
23% of samples, nearly all `_update_instance()`. Inside it:
`DynamicBVH::update` 26%, its own lines 25% (most on the loop that marks
every paired light's shadow dirty), `AABB::quantize` 15%, light pairing
13%.

`AABB::quantize()` calls `Math::fposmodp()` six times, and the UCRT
`fmodf` behind it computes an exact remainder in a loop.

# Change

`_aabb_quantize_pow2()` in `renderer_scene_cull.cpp`, used only where
`_update_instance()` quantizes, with a unit from `ldexp(1, n)`: for a
power-of-two unit, `x * (1 / unit)` and `floor()` are exact, and
`x - floor(x / unit) * unit` is the same real number as `fposmodp()`'s
remainder (plus the unit for negative x), so both round alike. A float32
comparison over 259 million values found no difference. Units below
`CMP_EPSILON`, where `x / unit` could overflow, still take
`AABB::quantize()`. `AABB::quantize()` itself is unchanged (other callers
pass any unit).

# Results

`level_bench.gd --frames=600`, three rounds, builds alternated (medians;
ranges do not overlap for city):

| Preset | Frame before / after (ms) | Render CPU before / after (ms) |
| --- | --- | --- |
| town (400 moving) | 1.165 / 1.169 | 0.630 / 0.621 |
| city_extreme (5,000 moving) | 8.553 / 8.421 | 5.225 / 5.103 |

# Not done

The loop that marks every paired light's shadow dirty for a moving
caster (`make_shadow_dirty()`, about 3% of city's main thread) walks
the unshadowed omni lights too. A list of only shadowed lights per
geometry would have to follow shadow toggles on lights, which the scene
cull reads from light storage each frame, and the dirty count also
drives the multi-camera caster culling. Left for now.
