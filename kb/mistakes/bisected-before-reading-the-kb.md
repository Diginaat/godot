---
type: Mistake
title: Started a rebuild bisect before searching the KB for the symptom
description: DDGI flicker came back in 0.10.1; a commit bisect (one build per step, some near-full) was started before the KB's render graph finding, which settles it in two runs without a build.
tags: [mistake, debugging, ddgi, render-graph, workflow]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What happened

The user saw spotted probes in the DDGI smoke test. The flicker test gave
`flicker_px` 8-16% against the 0.01-0.19% baseline. Release editors
narrowed it to 0.9.1 (clean) -> 0.10.1 (bad), and turning off each GPU
geometry setting alone changed nothing. Then a `git bisect` over 62
commits was started: one build per step, and steps across the core header
changes (D012-D014) rebuild almost the whole engine. The first attempt
also ran SCons under git's sh (`git bisect run`), so every build failed
and was skipped.

The user asked to check the KB first. The
[render graph finding](/ddgi/findings/render-graph-bounce-loss.md)
describes this class of problem and gives two runtime switches. Two runs
with `GODOT_RG_REORDER=0` or `GODOT_RG_FULL_BARRIERS=1` brought the
flicker back to baseline: the cause was found without any build.

# Rule

- Before a bisect or any rebuild to localize a regression, search
  `kb/` for the symptom and the subsystem (`grep -ril "<subsystem>" kb`),
  and read the findings and the successes that match.
- For a GPU result that changes when unrelated passes are added, run the
  render graph switches first ([success](/successes/env-switch-bisect.md)).
- If a bisect is still needed, drive it from PowerShell, not
  `git bisect run` (SCons can't configure under git's sh).
