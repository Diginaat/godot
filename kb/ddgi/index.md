# DDGI

* [DDGI overview](/ddgi/overview.md) - the probe GI system as built, and where its limits are

# Findings

* [Render graph ordering can drop the bounce light](/ddgi/findings/render-graph-bounce-loss.md) - open issue: compute work recorded before the DDGI apply pass made probes lose their bounce light
* [Compare DDGI only after convergence](/ddgi/findings/measure-after-convergence.md) - at 120 frames the budgeted scheduler hasn't settled; differences there aren't bugs
* [Extra code in the apply pass costs even when it does no work](/ddgi/findings/shader-code-cost-without-work.md) - per-pixel side work belongs in a separate small pass
* [A changing set of favored probes shows as flicker](/ddgi/findings/scheduler-favored-set-stability.md) - scheduler priority inputs must be stable over many frames

# Improvement proposals

* [Improvement proposals, ranked](/ddgi/improvements/index.md) - what the papers suggest for this DDGI, by value and cost
