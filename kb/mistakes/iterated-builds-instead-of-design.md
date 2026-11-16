---
type: Mistake
title: Built and benched five variants of one milestone instead of analysing first
description: Scene runtime M4 went through five designs, each built and benchmarked, plus probe, control and mutation builds (about 15 builds); profiling and a per-node cost estimate first would have pointed at the final design after one or two.
tags: [mistake, process, builds, scene-runtime]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-16T20:00:00+01:00 }
---

# What happened

M4 (stop transform propagation at dirty children) needs every dirty node
that wants transform notifications to be queued. The variants, in order:
clean each node in its `NOTIFICATION_TRANSFORM_CHANGED` handler; a control
with the early-out off; clean after the handlers in the flush (a cast and
an `ObjectDB` lookup per node); an epoch per flush instead of cleaning; the
epoch passed down the recursion; the epoch field moved next to the dirty
bits. Each one was a full rebuild (the `node_3d.h` change rebuilds most of
the engine) and a benchmark run. The first two variants added work to the
notification flush, which runs for every moving visual node in every game:
a design review of that hot path would have rejected them without a build.
Two builds were also lost to tooling: SCons started from Git Bash fails
at configure (known: build from PowerShell), and a padding edit used
`NO_INLINE` instead of Godot's `_NO_INLINE_`.

# Rule

Per milestone: read the code and profile the target workload first; write
down the per-node or per-call cost each candidate adds to hot paths
(notifications, propagation, flushes); pick one design; build once; compare
it with one control build. Build the editor (tests) and the release
template only for that design. Mutation checks (remove one guard, expect a
test failure) are worth their builds; probes are worth a build only when
they answer a question the profile cannot.
