# DDGI

* [DDGI overview](/ddgi/overview.md) - the probe GI system as built, and where its limits are

# Findings

* [Render graph ordering can drop the bounce light](/ddgi/findings/render-graph-bounce-loss.md) - compute work around the DDGI passes made probes lose their bounce light (2026-10) and flicker in spots (0.10.1, fixed 2026-11-27 with a timestamp fence after DDGI); the untracked access is not identified
* [Compare DDGI only after convergence](/ddgi/findings/measure-after-convergence.md) - at 120 frames the budgeted scheduler hasn't settled; differences there aren't bugs
* [Extra code in the apply pass costs even when it does no work](/ddgi/findings/shader-code-cost-without-work.md) - per-pixel side work belongs in a separate small pass
* [Favoring visible probes too much adds flicker](/ddgi/findings/scheduler-favored-set-stability.md) - 3x the update rate added flicker, 2x does not
* [A full resolution pass has a floor of about 0.2 ms](/ddgi/findings/full-res-pass-floor.md) - reading depth and normals and writing the GI buffers at 1080p; measure where the time goes before optimizing
* [Probes put nearby bounce light in the wrong place](/ddgi/findings/near-field-blob.md) - a lagging blob around the nearest probes; was fixed with near-field rays, back since they were removed
* [Near-field rays left dark spots that moved with the camera](/ddgi/findings/near-field-camera-spots.md) - screen-space history; the rays were removed from the engine
* [DDGI rays through glass ended wrong](/ddgi/findings/glass-probe-rays.md) - four termination and tint bugs at refractive surfaces
* [Scrolled probes traced at another probe's position](/ddgi/findings/negative-modulo-scroll.md) - GLSL `%` on negative values in `ddgi_probe_logical()`; black squares with infinite_world
* [Automatic reflections leaked an instance every frame on the dummy renderer](/ddgi/findings/headless-auto-reflection-leak.md) - headless runs; keyed creation on the instance, not the empty probe RID
* [Indirect light trails behind a moving lamp](/ddgi/findings/moving-light-trailing.md) - open issue: what limits the response, two rejected fixes, options

# Improvement proposals

* [Improvement proposals, ranked](/ddgi/improvements/index.md) - what the papers suggest for this DDGI, by value and cost
