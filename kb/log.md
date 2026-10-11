# Update log

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
