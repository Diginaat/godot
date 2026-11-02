# Update log

## 2026-11-02 (planar distance limits, presets)

* **Updated**: REFLECTIONS.md settings (`planar_max_distance`,
  `planar_mirror_max_distance`), findings, performance plan P7.

## 2026-11-02 (mirrors first for the mirror pass)

* **Updated**: REFLECTIONS.md settings and findings: upright reflectors get
  the mirror pass before floors.

## 2026-11-02 (mirror pass shadow LODs)

* **Updated**: REFLECTIONS.md performance plan P4: shadow passes in the
  mirror pass use the view's whole projection for LODs; mirror occlusion
  culling deferred.

## 2026-11-02 (probe recaptures with a moving sun)

* **Updated**: REFLECTIONS.md performance plan P6: measured, bounded, no
  change needed.

## 2026-11-02 (sky flashing in the mirror)

* **Updated**: REFLECTIONS.md issue J and findings (fog volume created
  before the sky is set up; no reprojection on a new volume's first frame).
* **Added**: mistakes/metric-blind-to-the-artifact.md.

## 2026-11-02 (mirror pass LODs)

* **Updated**: REFLECTIONS.md performance plan: the mirror pass picks mesh
  LODs from the view's whole projection and its own resolution (P4).
* **Added**: mistakes/test-scene-without-hard-quit.md.

## 2026-11-01 (brightness against the path tracer)

* **Added**: mistakes/tonemapped-noise-averages-darker.md.
* **Updated**: REFLECTIONS.md issue I and findings; probe emissive-light limitation.

## 2026-11-01 (DDGI in probe captures, performance plan)

* **Updated**: REFLECTIONS.md: `probe_capture_ddgi`, step 9 done, issue H
  done, performance plan, findings; reflections/planar-open-issues.md.

## 2026-11-01 (reflections: bugs seen in play)

* **Added**: mistakes/motion-test-that-did-not-move.md.
* **Updated**: REFLECTIONS.md findings (mirror pass selection by screen
  coverage, edge halo, mirror pass jitter) and open issues H and I;
  reflections/planar-open-issues.md.

## 2026-11-01 (reflections: open issues)

* **Added**: mistakes/stale-generated-shader-header.md.
* **Updated**: REFLECTIONS.md open issues (A, B, F done; C physical; D
  and G status), findings; reflections/planar-open-issues.md.

## 2026-11-01 (glass research)

* **Added**: research/cheap-glass-shading.md.

## 2026-11-01 (mirror pass cost)

* **Updated**: REFLECTIONS.md findings and limitations: mirror pass cropped
  to the reflector, no post-processing; timestamps before and after.

## 2026-11-01 (mirror pass, step 8)

* **Updated**: REFLECTIONS.md: `planar_mirror_render`, `planar_mirror_scale`,
  findings (clip distances, light culler winding, DDGI sharing, hybrid
  resolve, cost); reflections/planar-open-issues.md.

## 2026-11-01 (reflection resolution, mirrored-camera research)

* **Added**: research/mirrored-camera-reflections.md (step 8 sources).
* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflection_resolution`.

## 2026-11-01 (mirror probes)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_mirror_probes` (a probe in
  front of each mirror, for what close mirrors reflect off screen),
  findings log; reflections/planar-open-issues.md.

## 2026-11-01 (planar quality, paused)

* **Added**: reflections/planar-open-issues.md; REFLECTIONS.md "Open issues"
  (A to G) with repro positions and movement test commands.
* **Updated**: REFLECTIONS.md findings: temporal filter, footprint fill,
  coverage, normal check, automatic scan bounds and filters.

## 2026-11-01 (automatic reflections)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflections` (volume probe and
  planar reflectors found from mesh planes and material roughness).

## 2026-10-31 (reflections)

* **Added**: REFLECTIONS.md (runbook) and reflections/: overview and the
  architecture decision (pixel-projected planar reflections, budgeted and
  DDGI-normalized reflection probes, atlas on demand).
* **Added**: research/pixel-projected-reflections.md,
  research/cubemap-normalization.md.
* **Added**: mistakes/powershell-single-arg-splat.md,
  successes/reflection-atlas-on-demand.md.
* **Updated**: ddgi/improvements/specular-occlusion.md: probe reflections
  are now normalized by DDGI.

## 2026-10-31 (sync)

* **Updated**: CUSTOM_BUILD.md sync log: upstream `95c0fa5fe8` (4.8-beta),
  physx module `97bd583c28`. Shader `CONTAINER_VERSION` 5 -> 6 (upstream
  added a container header). Smoke: PhysX GPU, path tracer and DDGI pass;
  the PhysX tests report one error only because the checkout's `bin/` has
  no Streamline DLLs.

## 2026-10-31 (near-field rays removed, glass rays)

* **Added**: ddgi/findings/near-field-camera-spots.md: near-field rays
  removed from the engine (dark spots moving with the camera).
* **Added**: ddgi/findings/glass-probe-rays.md: four bugs on DDGI rays at
  refractive surfaces, fixed.
* **Updated**: near-field proposal and success marked deprecated.

## 2026-10-12 (performance and stability pass)

* **Updated**: DDGI.md findings: baseline per pass, apply sub-pass
  timestamps, reflection buffer written once, upsample block loads, the
  dropped distance blend skip, the `--chaos` stability test.
* **Updated**: tooling/vulkan-sync-validation.md: a too old layer reports
  false SPIR-V errors.

## 2026-10-12 (dark places)

* **Updated**: DDGI.md step 30: no flicker or blotches in dark places;
  new `tunnel` test view.

## 2026-10-12 (moving objects of any shape)

* **Updated**: DDGI.md step 29: moving objects as oriented boxes with exact
  face form factors (0.7.1).

## 2026-10-12 (moving objects vs path tracer)

* **Updated**: DDGI.md step 28: moving objects' light matched against the
  path tracer.

## 2026-10-12 (moving objects, no noise)

* **Updated**: DDGI.md step 27: moving objects lit as spheres by one ray
  per block, no noise.

## 2026-10-12 (moving objects)

* **Updated**: DDGI.md step 26: probes skip moving objects, near-field
  history lets go faster near them (the smears behind a moving ball).

## 2026-10-12 (near-field rays)

* **Added**: [near-field rays](/ddgi/improvements/near-field-rays.md)
  (proposal 12, done), finding [probes put nearby bounce light in the
  wrong place](/ddgi/findings/near-field-blob.md), success
  [near-field rays](/successes/near-field-rays.md), mistakes
  [a test setting in the animation callback](/mistakes/test-setting-in-animation-callback.md)
  and [a long run of an unfit scene](/mistakes/long-run-of-an-unfit-scene.md).
* **Updated**: [test protocol](/tooling/ddgi-test-protocol.md) with the
  bleed view tests (still and animated).
* **Verified**: emissive surfaces as direct lights (previous entry): the
  storm level shows a smooth light pool from the moving sphere.

## 2026-10-12 (emissive lights, work in progress)

* **Added**: references [DDGI papers and reference code](/research/majercik-ddgi-reference.md)
  and [O3DE DiffuseProbeGrid](/research/o3de-diffuse-probe-grid.md).
* **In progress**: emissive surfaces as direct lights in the DDGI apply pass
  (`rendering/global_illumination/ddgi/emissive_lights`), against light
  spots, blocky glow and bleeding from small emitters. Not verified yet.

## 2026-10-12 (least-squares bake)

* **Implemented**: [least-squares baking](/ddgi/improvements/least-squares-bake.md)
  on `dev-ddgi-quality`; [success](/successes/least-squares-bake.md).
* **Reread** the six GI papers for these two features: SDFDDGI's min/max
  checkerboard went into the low resolution apply; Enemies' dilation
  caveat (it creates new leaks) kept probes inside geometry out of the
  solve and its smoothness term.

## 2026-10-12 (low resolution apply)

* **Implemented**: [low-resolution apply](/ddgi/improvements/low-res-apply-directional.md)
  on `dev-ddgi-quality`, default 1/4: DDGI 1.40 -> 1.13 ms at 1080p.
* **Added**: finding [full resolution pass floor](/ddgi/findings/full-res-pass-floor.md),
  mistake [encoding convention mismatch](/mistakes/encoding-convention-mismatch.md).

## 2026-10-12 (second research set)

* **Added**: 6 references (Unity 6 APV, tetrahedral interpolation, CoD:
  Infinite Warfare precomputed lighting, Stupid SH Tricks, GI downscaling,
  FF7 Rebirth GI) and an unread stub for a Zhihu article (HTTP 403).
* **Added**: proposals 9-11 (low-resolution apply with directional output,
  specular occlusion, least-squares baking) and two small ideas.

## 2026-10-12 (scheduler 2x, moving lights)

* **Corrected**: [favored probes and flicker](/ddgi/findings/scheduler-favored-set-stability.md):
  the cause was the 3x rate, not the window; shipped 2x.
  [Mistake recorded](/mistakes/three-runs-of-a-noisy-test.md).
* **Added**: [moving light trailing](/ddgi/findings/moving-light-trailing.md)
  (open issue, two rejected fixes), the emissive step and weather stepping
  tests in [the test protocol](/tooling/ddgi-test-protocol.md).
* **Updated**: sample-driven results in the proposal and the success page.

## 2026-10-12 (mistakes and successes)

* **Added**: [mistakes](/mistakes/) (7) and [successes](/successes/) (6)
  sections, the `Mistake` and `Success` types, and rule 8 in
  [the howto](/kb-howto.md): record both as they happen.

## 2026-10-12 (later)

* **Added**: [DDGI test protocol](/tooling/ddgi-test-protocol.md), from
  the measurements for sample-driven updates: step response with
  `--sun_step`, startup not reproducible, flicker spread over 3 runs,
  A/B with one editor copy per variant, and a storm game-level test with a
  moving emissive object (fixed exposure, `--fixed-fps 60`, averages
  only).
* **Added**: findings [extra code in the apply pass costs](/ddgi/findings/shader-code-cost-without-work.md)
  and [favored set stability](/ddgi/findings/scheduler-favored-set-stability.md).
* **Updated**: [sample-driven updates](/ddgi/improvements/sample-driven-updates.md)
  is implemented on `dev-ddgi-sched`, with results.

## 2026-10-12

* **Initialization**: Created the bundle: project overview, DDGI and XeGTAO
  state, the findings from the XeGTAO work (render graph ordering issue,
  convergence measuring), six GI papers and talks with what they mean for
  DDGI, and a ranked list of DDGI improvement proposals.
