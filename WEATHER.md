# Weather and wind

Native C++ weather for this build: one global wind that grass, trees, cloth
and physics bodies all follow, and (in later steps) a weather node that blends
between weather types and drives the sky, rain, fog, lightning and sound. The
design is a port of a GDScript weather system, rebuilt so that new weather
types and new effects can be added without touching the core.

Code: `modules/weather/` (turn it off with `module_weather_enabled=no`).
Branch: `dev-weather` (from `dev`), merged into `dev` after each finished step
passes the build, the smoke tests and the wind tests below.

## Design

| Piece | Role | Step |
| --- | --- | --- |
| `WindServer` (singleton) | The world's wind: mean speed and heading, gusts that travel with the wind, direction swing, turbulence. `sample(position)` on the CPU; the same noise in shaders through three global shader parameters. | 1 |
| `res://.builtin/wind.gdshaderinc` | Built-in shader include served from memory: `wind_sample()`, `wind_grass_offset()`, `wind_tree_offset()`, `wind_to_local()`. | 1 |
| `WindReceiver3D` | Child of a `RigidBody3D`: aerodynamic drag from the wind, at the node's position (torque when off-centre), area from the collision shapes. | 1 |
| `GrassField3D` default material | Blades bend with the wind when the module is built. | 1 |
| `PhysXCloth3D.use_global_wind` | Cloth adds the world's wind. | 1 |
| `WindZone3D` | Local wind on top of the global one (downwash, blasts, vortices). | later |
| `WeatherProfile` (resource) | One weather state (13 built-in parameters). Blending walks the property list, so a new parameter is one enum entry and one property, and script subclasses blend their own exported values. | 2 |
| `WeatherLibrary` (resource) | The weather types by name. A new type is a new profile in a library, no code; the built-in 11 come from `WeatherLibrary.get_default()`. | 2 |
| `Weather` (node) | Picks and blends profiles, drives the `WindServer`, wetness globals, runs its effects, editor preview. Scene values go through `drive()`, restored before save and on exit. | 2 |
| `WeatherEffect` (resource) | Listed in `Weather.effects`, settings edited inline. Virtual `_start`, `_stop`, `_update`, `_apply_quality`. Rain, haze, mist motes, lightning, sky and sound are effects; snow or hail later are new subclasses (GDScript too). | 2 |
| `WeatherLightingEffect` | Sun, sky light, exposure, volumetric and distance fog. | 2 |
| `WeatherSkyEffect` | Sky and clouds: drives a weather-ready sky material, or shows the built-in weather sky in place of the scene's. Clouds drift with the wind aloft. | 3 |
| `WeatherRainEffect` | GPU raindrops and impacts around the camera, collision height field, top-down rain occlusion map. | 4 |
| `res://.builtin/weather_wetness.gdshaderinc`, `res://.builtin/wet_surface.gdshader` | Wet surfaces: darkening in patches, puddles with ripples, water down walls, dry under cover. Served from memory; exports leave them out (`WeatherExportPlugin`). | 4 |
| Ground haze, mist motes | Volumetric haze and near-camera droplets. | 5 |
| Lightning | Bolts, flashes, thunder timing. | 6 |
| Sound | Rain, wind and thunder synthesized in C++ at startup; own files override. | 7 |

Native class names differ from the GDScript system's (`WeatherSystem`,
`WeatherPreset`, `RainEmitter`, ...), so a project using the GDScript version
keeps working until it switches.

## Steps

| Step | What | Status |
| --- | --- | --- |
| 1 | Wind core: `WindServer`, shader include, `WindReceiver3D`, grass default material, PhysX cloth | Done |
| 2 | Weather core: `WeatherProfile`, `WeatherLibrary`, `Weather` node, `WeatherEffect`, `WeatherLightingEffect`, scene driving, wetness | Done |
| 3 | Sky and clouds: `WeatherSkyEffect`, built-in weather sky | Done |
| 4 | Rain, occlusion, wet surfaces: `WeatherRainEffect`, built-in wetness include and wet surface shader | Done |
| 5 | Ground haze, mist motes | Next |
| 6 | Lightning | |
| 7 | Sound synthesis | |

## Weather core

A `Weather` node in the level picks a type by name (`weather`, a dropdown of
the library's names plus `Custom`) and blends every blendable property of
the profiles over `transition_time` (smoothstep). The target profile is
re-read every frame, so edits to it show at once. When the target is a
different class or script than the profile being shown, the transition
starts from a fresh instance of the target's kind (script defaults) filled
with the values showing, so script-only values blend from their defaults.

Each frame: blend, drive the `WindServer` (speed, heading, gustiness,
swing), sample the gust at the camera for rain bursts, compute the horizon
light, update wetness and the `weather_wetness` / `weather_rain` globals,
then run the effects in list order: the sky first, so the lighting takes
its colours, and the lighting before rain and haze, so they see its
horizon light.

`drive(object, property, value)` remembers the object's own value. A
value that differs from the last one written (an inspector edit, a
day-night script) becomes the new base. `restore_scene()` runs before every
editor save and when the node leaves the tree, so the weather never ends up
in the scene file. `release(object, property)` gives one property back
(for an effect that stops).

Effects are resources in `Weather.effects` (local to scene), so their
settings are edited inline and saved with the scene. The node starts with
`Weather.create_default_effects()`. An effect added, removed or disabled at
runtime or in the inspector is started or stopped on the next frame.

## Sky

`WeatherSkyEffect` looks at the environment's sky when it starts:

* a `ShaderMaterial` whose shader has a `cloud_coverage` uniform is
  driven in place (`cloud_coverage`, `cloud_density`, `cloud_darkness`,
  `overcast`, `cloud_offset`) through `drive()`, so its saved values never
  change;
* anything else is replaced, while the effect runs, by the built-in weather
  sky (`WeatherSkyEffect.create_default_material()`), in its own `Sky` with
  the scene sky's radiance size and process mode. A `ProceduralSkyMaterial`
  passes on its colours. The environment's `sky` and `background_mode` go
  through `drive()`.

The built-in sky is the GDScript system's sky shader, kept as a C++ string
(`effects/weather_sky_shader.h`), with its two noise textures built in code.
Its look is changed by saving `create_default_material()` as a resource and
using that as the environment's sky. Every frame the effect passes the
sky's horizon colour, overcast grey and energy to `Weather.set_sky_colors()`,
which the lighting uses for the horizon light and the distance fog.

Clouds drift with the wind aloft: `cloud_wind_multiplier` (3) times the
wind's speed, turned by `cloud_wind_veer_degrees` (15). The shader adds the
offset to its lookup, so the effect subtracts the drift, and wraps it at
`1 / cloud_scale` meters, where both textures repeat (the detail texture
repeats there too as long as `detail_scale` is a whole number).

The lightning step adds `lightning_flash`, `lightning_direction` and
`lightning_color` through `get_sky_material()`.

## Rain and wet surfaces

`WeatherRainEffect` builds its nodes as internal children of the `Weather`
node when it starts and frees them when it stops; nothing is saved. A
property change or a quality change rebuilds them on the next frame (the old
nodes leave the tree at once, so the new ones keep their names).

* **Drops**: `GPUParticles3D` in a box of `area_size` around the camera,
  emitted upwind so slanted rain lands around it. `amount_ratio` follows
  `Weather.get_rain_amount()`, so the rain never restarts. The streak shader
  gets the horizon light every frame.
* **Impacts**: a sub-emitter at collision, with draw passes for the ring,
  the crown sheet and (from Medium) the droplet crown.
* **Collision**: a `GPUParticlesCollisionHeightField3D` following the
  camera, recaptured every `collision_refresh_interval` while it rains.
* **Occlusion map**: an orthographic camera in a `SubViewport` looks down
  from `occlusion_capture_height`; a full-screen pass writes world height
  into a float texture. It is rendered once per `occlusion_update_step` of
  camera movement, and only while something is wet or it rains.

| Quality | Drops | Impacts | Crown droplets | Collision | Occlusion map |
| --- | --- | --- | --- | --- | --- |
| Low | 8000 | 2500 | 0 | 256 | 256 |
| Medium | 14000 | 5000 | 4 | 512 | 512 |
| High | 24000 | 8000 | 6 | 512 | 512 |
| Ultra | 40000 | 12000 | 8 | 1024 | 1024 |

Surfaces get wet only through a shader that includes the wetness functions:
`res://.builtin/wet_surface.gdshader` (albedo, roughness and normal
textures plus wetness) or `weather_apply_wetness()` from
`res://.builtin/weather_wetness.gdshaderinc` in a project's own shader. The
module registers the globals `weather_rain_occlusion` (sampler2D) and
`weather_occlusion_area` (vec4) next to `weather_wetness` and
`weather_rain`. A material saved with the built-in shader refers to it by
path; `WeatherExportPlugin` leaves `res://.builtin/` paths out of exports,
since the game has them in memory.

## Wind

The wind at a point is the mean wind plus gusts. Gusts are two octaves of
value noise, `gust_scale` meters across, that move with the mean wind, so gust
fronts sweep across a field at the wind's speed (`speed * (1 + gustiness *
(gust * 1.4 - 0.4))`, as in the GDScript system). Gusts also swing the heading
by up to `direction_swing_degrees * gustiness`. The pattern offset wraps at
512 gust lengths, a whole period of every octave, so it never loses precision.

The noise is written twice, in C++ (`WindServer::noise()`,
`WindServer::sample_state()`) and in the include (`wind_noise()`,
`wind_sample()`); keep them identical. The wind test below compares them.

Shader globals (`vec4`, registered by the module before any scene loads):
`wind_mean` (direction x, z, speed, gustiness), `wind_gust` (offset x, z,
1 / gust size, swing radians), `wind_motion` (drift, turbulence). A project
that declares the same names (as `vec4`) just keeps working.

Usage:

```glsl
shader_type spatial;
#include "res://.builtin/wind.gdshaderinc"

void vertex() {
	// Grass blade: root at the origin, UV.y 0 at the root to 1 at the tip.
	vec3 root = MODEL_MATRIX[3].xyz;
	float height = length(MODEL_MATRIX[1].xyz);
	VERTEX += wind_to_local(MODEL_MATRIX, wind_grass_offset(root, UV.y, height, 5.0, TIME));
}
```

Trees: `wind_tree_offset(world_vertex, root, height, stiffness, branch, leaf,
world_normal, TIME)`, with branch and leaf weights from vertex colour or a
height mask. The trunk leans downwind and rocks at a frequency that drops with
height, branches bob, leaves flutter along their normal.

Physics: add a `WindReceiver3D` under a `RigidBody3D`. The force is clamped so
it can at most bring the body to the wind's speed in one physics step (light,
wide bodies would otherwise shake), and sleeping bodies are only woken above
`wake_acceleration`.

## Tests

The wind test project (written per run, not kept) checks:

| Check | Result (2026-11-07, Vulkan, RTX 3060) |
| --- | --- |
| `WindServer` singleton exists, 90 degrees blows toward +X | pass |
| Mean of 400 samples near the speed (10 m/s, gustiness 0.6) | 11.2 m/s (gusts average +18% at gustiness 0.6, by design) |
| Gusts vary the speed | 7.6 to 16.0 m/s |
| GPU `wind_sample()` (canvas shader into a half-float target) against `WindServer.sample()`, 16x16 points over 400 m | worst 0.043 m/s |
| 2 kg unit box, no gravity, 4 s in the wind | 8.6 m/s downwind, area 1.14 m² |
| `GrassField3D` default material with wind, a tree shader with `wind_tree_offset()` | compile without errors; grass bends downwind at 4 and 15 m/s, upright at 0 |

Weather core test (headless, 24 checks, 2026-11-07): starts Clear and drives
the wind to 2 m/s; script effect started, updated and given quality High;
11 types plus Custom; instant Storm hides the sun, dims the sky to 0.55, turns
on distance fog 0.007, rain 0.82 with gusts, wetness 1.0 after 0.6 s with
`wetting_time` 0.5; a 2 s transition is at 8 m/s halfway and 2 m/s at the
end; pre-save restores sun, sky and fog and the next frame drives again; an
outside edit becomes the base; a script profile's own value blends (0.5
halfway); a removed effect is stopped; leaving the tree restores the sun and
the wind. A saved scene keeps `weather` and the effect's settings and nothing
else; the editor opens it without errors.

Sky test (headless, 17 checks, 2026-11-07): default effects are sky, then
lighting; a procedural sky is replaced by the built-in one (background
switched to the sky, top colour kept); Clear coverage 0.15, Storm 1.0 with
overcast 0.92; the offset moves exactly against the wind aloft; the weather
reads the sky's horizon colour; pre-save restores the scene's sky and
background and the next frame shows the weather sky again; disabling the
effect gives the scene's sky back; a scene sky made from
`create_default_material()` is kept and driven, and pre-save and exit
restore its own values. Screenshots on Vulkan: Clear, Partly Cloudy,
Overcast and Storm look as in the GDScript system. The weather core test
still passes (24 checks).

Rain test (2026-11-07, 20 checks headless, 21 on Vulkan): the built-in
include and wet surface shader load, the globals exist; Clear has no drops
and no occlusion capture; the rain follows the camera; High quality has
24000 drops, 8000 impacts, 3 impact passes; Storm drops at the rain amount
(0.83), falling downwind and spawned upwind; the drops get the horizon
light; the occlusion map is centered on the camera, with 3.10 m under a
3.1 m roof and 0.00 m on open ground; wet ground darkens to 0.77 of its dry
brightness while the ground under the roof stays at 1.00; Low quality
rebuilds with 8000 drops; a changed area rebuilds; dry weather turns the
map off; disabling the effect frees its nodes; leaving the tree clears the
map. A saved material with the built-in wet shader refers to it by path and
loads in a new process; the editor opens a scene with the weather without
errors and leaves the file unchanged.

## Findings

* `RenderingServer.global_shader_parameter_add()` at the module's scene
  level runs before `global_shader_parameters_load_settings()`, which only
  sets parameters that already exist. So module globals and project globals
  of the same name and type live together.
* `ResourceLoader::exists()` and `load()` look in the resource cache first,
  so a `ShaderInclude` with a path taken over in memory works for
  `#include` without a file, in the editor and in exported games. A
  dot-folder path (`res://.builtin/`) never clashes with project files.
* `RenderingServer.frame_pre_draw` is emitted on the main thread in
  `draw()`, also with the separate render thread, so the wind advances there.
* `Script::get_property_default_value()` returned nothing for a GDScript
  inner class at runtime, so a profile of a new script class can't take
  its defaults from the script. A fresh instance of the class does.
* Godot doc XML: `&` in a code sample (`&"weather"`) must be written
  `&amp;`, or the whole file fails to parse.
* `RenderingServer.global_shader_parameter_get()` only works in the editor
  (and with the dummy renderer), so effects keep the values they set
  (`WeatherRainEffect.get_occlusion_area()`) for scripts and tests.
* Nodes freed with `queue_free()` keep their names until the end of the
  frame: rebuilt nodes added under the same name got renamed. Remove them
  from the tree first.
* Shader includes are not resource dependencies, so a shader that
  includes a `res://.builtin/` file exports cleanly; a material that uses a
  built-in shader lists it as a dependency, hence the export plugin.
