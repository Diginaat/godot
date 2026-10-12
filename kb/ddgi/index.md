# DDGI

* [DDGI overview](/ddgi/overview.md) - the probe GI system as built, and where its limits are

# Findings

* [Render graph ordering can drop the bounce light](/ddgi/findings/render-graph-bounce-loss.md) - open issue: compute work recorded before the DDGI apply pass made probes lose their bounce light
* [Compare DDGI only after convergence](/ddgi/findings/measure-after-convergence.md) - at 120 frames the budgeted scheduler hasn't settled; differences there aren't bugs
* [Extra code in the apply pass costs even when it does no work](/ddgi/findings/shader-code-cost-without-work.md) - per-pixel side work belongs in a separate small pass
* [Favoring visible probes too much adds flicker](/ddgi/findings/scheduler-favored-set-stability.md) - 3x the update rate added flicker, 2x does not
* [A full resolution pass has a floor of about 0.2 ms](/ddgi/findings/full-res-pass-floor.md) - reading depth and normals and writing the GI buffers at 1080p; measure where the time goes before optimizing
* [Probes put nearby bounce light in the wrong place](/ddgi/findings/near-field-blob.md) - a lagging blob around the nearest probes; fixed with near-field rays
* [Indirect light trails behind a moving lamp](/ddgi/findings/moving-light-trailing.md) - open issue: what limits the response, two rejected fixes, options

# Improvement proposals

* [Improvement proposals, ranked](/ddgi/improvements/index.md) - what the papers suggest for this DDGI, by value and cost
