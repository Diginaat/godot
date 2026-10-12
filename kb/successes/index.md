# Successes

Approaches that worked in this project and are worth reusing: what was
done, why it worked, and when to use it again. Add one whenever something
works clearly better than what came before. Mistakes are in
[mistakes](/mistakes/).

* [Sample-driven probe scheduling](/successes/sample-driven-scheduling.md) - DDGI follows light changes about 8% faster at no measurable cost
* [Measuring the response to a change](/successes/step-response-instead-of-startup.md) - converge, step one input, trace: repeats within a few frames
* [One editor copy per variant](/successes/editor-copy-per-variant.md) - A/B tests without rebuilding
* [Side work in its own small pass](/successes/separate-small-pass.md) - 0.03 ms instead of 0.05 ms in the hot shader
* [Render graph switches to narrow a GPU problem](/successes/env-switch-bisect.md) - ordering vs data in two runs
* [A real game level in the test suite](/successes/game-level-test.md) - finds visual problems the synthetic scenes miss
