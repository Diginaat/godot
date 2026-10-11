# Mistakes

Mistakes agents made in this project: what went wrong, why, and the rule
that prevents it. Read these before testing or measuring. Add one whenever
something goes wrong: a false claim, a wasted rebuild, a test that
measured the wrong thing, a correction from the user.
What worked is in [successes](/successes/).

* [Reported a bug from runs that hadn't converged](/mistakes/conclusions-from-unconverged-runs.md) - repeat and converge before claiming a difference
* [Metrics that measured noise](/mistakes/metric-measured-noise.md) - measure the noise floor first, then pick thresholds
* [Ran a GI test with auto exposure and real-time timing](/mistakes/uncontrolled-test-conditions.md) - pin everything that adapts or animates over time
* [Assumed shader cost scales with work](/mistakes/assumed-cost-scales-with-work.md) - measure the code compiled out first
* [Tuned for speed before checking flicker](/mistakes/tuned-speed-before-stability.md) - run the whole suite per variant
* [Test script bugs looked like engine bugs](/mistakes/script-bugs-taken-for-engine-bugs.md) - fail fast on script errors, check tests on known answers
* [Quoting broke generated scripts](/mistakes/generated-code-quoting.md) - put code in files, make edits all-or-nothing
