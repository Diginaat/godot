# DDGI

* [DDGI overview](/ddgi/overview.md) - the probe GI system as built, and where its limits are

# Findings

* [Render graph ordering can drop the bounce light](/ddgi/findings/render-graph-bounce-loss.md) - open issue: compute work recorded before the DDGI apply pass made probes lose their bounce light
* [Compare DDGI only after convergence](/ddgi/findings/measure-after-convergence.md) - at 120 frames the budgeted scheduler hasn't settled; differences there aren't bugs

# Improvement proposals

* [Improvement proposals, ranked](/ddgi/improvements/index.md) - what the papers suggest for this DDGI, by value and cost
