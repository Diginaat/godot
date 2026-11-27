---
type: Success
title: Sync points at labels to place a render graph hazard
description: A temporary hook that adds a graph synchronization point at a chosen label found where the DDGI flicker hazard sits, in one build.
tags: [render-graph, synchronization, debugging, ddgi]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What worked

After the render graph switches (see
[env-switch-bisect](/successes/env-switch-bisect.md)) showed the DDGI
flicker was an ordering problem, a temporary hook in
`RenderingDevice::draw_command_begin_label()` and `end_label()` called
`draw_graph.add_synchronization()` when the label contained the text in an
environment variable (`*` for every label). One build, then one run per
label: the sync point fixed the flicker from the TLAS build to the start of
the opaque pass, and not at render setup, the depth pre-pass, the sky or
TAA. That placed the hazard without a bisect or a rebuild per guess.

A one-frame dump of every graph command's level and tracked resources
(printed in `RenderingDeviceGraph::end()`) then showed that the tracked
resources were ordered right, so the hazard is an access the graph can't
see (buffer device addresses in the ray tracing shaders).

# Lessons

- A timestamp is a sync point only for the commands since the previous
  timestamp; a fence at the start of a block does not order the block
  against work recorded after it. Test the fence at both ends.
- Remove the hook and the dump before committing; they are a few lines to
  rewrite next time.
- Run the test with self-quitting arguments (`--measure`, `--shot`,
  `--quit-after`) so no window stays open.

See [render-graph-bounce-loss](/ddgi/findings/render-graph-bounce-loss.md).
