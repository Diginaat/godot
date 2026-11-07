---
type: Finding
title: World wind and the weather core
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

# Weather core (step 2)

* **Types are data.** `WeatherProfile` holds 13 indexed parameters;
  `WeatherLibrary` names them. A new weather type is a profile in a library.
* **Blending is generic.** It walks the property list (floats, colors,
  vectors; `degrees` hints the short way round), so script subclasses blend
  their own values. A transition to a new class starts from a fresh
  instance of it, because the script's default values aren't available
  through `Script::get_property_default_value()` at runtime.
* **Effects are resources** in `Weather.effects`, with virtual
  `_start/_stop/_update/_apply_quality`, edited inline and saved with the
  scene. `WeatherLightingEffect` is the first.
* **Scene values are borrowed.** `Weather.drive()` keeps the scene's own
  value (an outside edit becomes the new base) and `restore_scene()` puts it
  back before every editor save and on exit. 24 headless checks pass.

# Sky (step 3)

* **Drive or replace.** A sky shader with a `cloud_coverage` uniform is
  driven in place; any other sky is swapped for the built-in weather sky
  while the effect runs, both through `drive()`, so the scene file never
  changes. A `ProceduralSkyMaterial` passes on its colours.
* **No assets.** The built-in sky's shader is a C++ string and its two
  cloud textures are `NoiseTexture2D` resources built in code (the module
  depends on `noise`).
* **Clouds and wind.** The drift is subtracted (the shader adds it to its
  lookup) and wrapped where the textures repeat.

# Rain and wet surfaces (step 4)

* **Effects own internal nodes.** The rain's particles, collider and
  occlusion viewport are internal children of the `Weather` node, built on
  start and freed on stop, so they are never saved.
* **Built-in shaders by path.** The wetness include and a wet surface
  shader live in the resource cache under `res://.builtin/`. Includes are
  not dependencies; a material using the built-in shader is, so an export
  plugin skips those paths.
* **Occlusion only when it matters.** The top-down height map renders once
  per grid step of camera movement, and only while something is wet.
* **Read back what you set.** `global_shader_parameter_get()` is
  editor-only on real renderers; effects keep their own copy.

# Haze and mist motes (step 5)

* **Haze in the lowest air.** A camera-following `FogVolume` holds ground
  mist that thins with height, rain shafts and spray; the environment's own
  volumetric fog density moves into it, so the sky stays clear above.
* **Snap, don't slide.** The volume moves in world-snapped steps and the
  shader works in world space: a volume moved every frame smears the fog's
  temporal filter.
* **Effects find each other.** `Weather.get_effect(class)` lets the haze
  lean its shafts like the rain without depending on the effect order.
* **Detail fog can't show.** Floating droplets (`GPUParticles3D`) wrap
  inside a box around the camera and drift at a capped fraction of the wind.
