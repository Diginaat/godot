# Update log

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
