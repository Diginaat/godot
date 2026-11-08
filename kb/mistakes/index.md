# Mistakes

Mistakes agents made in this project: what went wrong, why, and the rule
that prevents it. Read these before testing or measuring. Add one whenever
something goes wrong: a false claim, a wasted rebuild, a test that
measured the wrong thing, a correction from the user.
What worked is in [successes](/successes/).

* [Editor theme validation](/mistakes/editor-theme-validation.md) - query the generated editor theme, not a Control's possibly null local theme

* [Build interpreter mismatch](/mistakes/python-build-interpreter.md) - check existing Python installations before installing SCons or rebuilding SDKs
* [Popup alpha without native composition](/mistakes/popup-alpha-without-native-composition.md) - compare desktop pixels as well as viewport alpha; account for screen origins
* [New translation-unit assumptions](/mistakes/new-translation-unit-assumptions.md) - check explicit includes, namespaces and container signatures in this fork
* [Native doc self-closing section](/mistakes/native-doc-self-closing-section.md) - validate embedded class help in editor startup as well as RST
* [Shared texture mip readback](/mistakes/shared-texture-mip-readback.md) - inspect owner mip chains, not assumed shared-view copy offsets
* [Deferred help after editor teardown](/mistakes/deferred-help-after-editor-teardown.md) - static worker callbacks need editor lifetime guards

* [Reported a bug from runs that hadn't converged](/mistakes/conclusions-from-unconverged-runs.md) - repeat and converge before claiming a difference
* [Metrics that measured noise](/mistakes/metric-measured-noise.md) - measure the noise floor first, then pick thresholds
* [Ran a GI test with auto exposure and real-time timing](/mistakes/uncontrolled-test-conditions.md) - pin everything that adapts or animates over time
* [Assumed shader cost scales with work](/mistakes/assumed-cost-scales-with-work.md) - measure the code compiled out first
* [Tuned for speed before checking flicker](/mistakes/tuned-speed-before-stability.md) - run the whole suite per variant
* [Test script bugs looked like engine bugs](/mistakes/script-bugs-taken-for-engine-bugs.md) - fail fast on script errors, check tests on known answers
* [Credited a fix on 3 runs of a noisy test](/mistakes/three-runs-of-a-noisy-test.md) - know the test's spread on the baseline first
* [Mixed up an encoding convention](/mistakes/encoding-convention-mismatch.md) - check the value range of shared helpers before pairing them
* [Quoting broke generated scripts](/mistakes/generated-code-quoting.md) - put code in files, make edits all-or-nothing
* [A test setting lived in the animation callback](/mistakes/test-setting-in-animation-callback.md) - still tests ran with the wrong sun; check one final image of a new scene
* [Single-flag benchmark runs measured the defaults](/mistakes/powershell-single-arg-splat.md) - cast splatted arguments to [string[]], call array-taking scripts in-process, check each flag took effect
* [A restored shader source didn't rebuild its generated header](/mistakes/stale-generated-shader-header.md) - touch restored files; check the .gen.h when a shader change doesn't show
* [Recorded a long run of a scene that couldn't show the problem](/mistakes/long-run-of-an-unfit-scene.md) - check one frame shows the effect before a long run
* [A walking test that didn't walk](/mistakes/motion-test-that-did-not-move.md) - check the first and last frames of a motion test differ as intended
* [A flicker metric that could not see the flicker](/mistakes/metric-blind-to-the-artifact.md) - check crops around each event; measure the artifact, not a proxy; reproduce the user's exact motion
* [A test scene that could not quit](/mistakes/test-scene-without-hard-quit.md) - give every automated run `--quit-after`, check for `SCRIPT ERROR` before looping
* [Static test expected a spot shadow pass every frame](/mistakes/cached-positional-shadows-in-tests.md) - positional shadows are cached; make the shadow dirty in frames that must render it
* [Camera-facing splats were wound as back faces](/mistakes/billboard-winding-under-cull-disabled.md) - wind generated billboards clockwise from the camera; `cull_disabled` flips NORMAL on back faces
* [Streaming pressure budget sized from the total data](/mistakes/pressure-budget-from-total-size.md) - measure per-view working sets before choosing a pressure budget
* [GPU stage check read timestamps after a texture readback](/mistakes/timestamps-after-texture-readback.md) - read RD timestamps before get_image(); a readback stalls and begins a new frame
* [Averaged noisy frames after the tonemapper](/mistakes/tonemapped-noise-averages-darker.md) - compare brightness in linear HDR at a fixed exposure; a reference must not depend on spp
* [GPU buffer copy followed by compute writes broke D3D12](/mistakes/d3d12-buffer-copy-then-compute-write.md) - seed a buffer that compute writes with buffer_update, not buffer_copy; run new GPU paths on D3D12 too
* [The viewport's GPU time missed work recorded before the viewport](/mistakes/viewport-time-misses-pre-viewport-work.md) - measure whole-frame GPU time for features that record work outside the viewport; never one GPU timestamp per object
* [A benchmark ran with the feature it measures inactive](/mistakes/bench-without-the-feature-active.md) - check counters and timings are nonzero before printing numbers; shared mesh+material batches CLOD objects out of CLOD
