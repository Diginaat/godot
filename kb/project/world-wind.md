---
type: Finding
title: World wind for foliage, cloth and bodies
description: WindServer singleton with travelling gusts, the same noise on CPU and GPU, a built-in shader include served from memory, and drag on rigid bodies.
tags: [weather, wind, foliage, physics, shaders]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-07T00:00:00+01:00 }
---

# Implemented

Step 1 of the native weather (see `WEATHER.md` at the repository root):
`modules/weather/` adds the `WindServer` singleton and `WindReceiver3D`.

* **One wind for everything.** Mean speed and heading plus gusts: two octaves
  of value noise that travel with the mean wind, so gust fronts cross a
  grass field at the wind's speed, and swing the heading. `sample(position)`
  on the CPU; three `vec4` global shader parameters for shaders.
* **Same noise twice.** C++ and GLSL versions of the noise are kept
  identical; on Vulkan the GPU result matched the CPU within 0.043 m/s at
  10 m/s over 256 points.
* **Shader include without a file.** A `ShaderInclude` whose path
  (`res://.builtin/wind.gdshaderinc`) is taken over in memory is found by
  `#include`, because the resource loader checks the cache first.
* **Globals before scenes.** The module adds the globals at the scene
  initialization level, before `global_shader_parameters_load_settings()`,
  which only sets names that already exist.
* **Bodies.** `WindReceiver3D` applies drag at its own position, area from
  the collision shapes as seen along the wind, clamped so one step never
  overshoots the wind's speed.
* **Grass and cloth.** `GrassField3D`'s default material bends with the wind
  when the module exists (`#define WIND`), and `PhysXCloth3D` adds the world
  wind through the `WindServer` singleton, looked up by name so neither
  depends on the module at compile time.
