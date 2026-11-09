---
type: Mistake
title: A test scene that could not quit
description: A scratch benchmark scene quit itself from its script; when the script failed to parse, an empty window stayed open on the user's screen until the shell timeout.
tags: [mistake, testing, tooling]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-02T00:00:00Z }
---

# What happened

- A throwaway benchmark project (mirror pass LOD test) ended itself with
  `get_tree().quit()` after N frames. A text edit broke the script's line
  endings, it failed to parse, and Godot showed an empty gray window. The
  only stop was a 200 s shell `timeout`, and the user saw the window and
  asked about it.

# Rule

- Give every automated run a hard engine-side stop: `--quit-after <frames>`
  on the command line, a little above what the script needs. It works even
  when the script doesn't load.
- Check the log for `SCRIPT ERROR` after the first run before looping over
  variants.
- Write scratch scripts whole (one write) rather than patching them with
  regular expressions.

# Recurrence (2026-11-10)

- The first `instance_bench` run called `RenderingServer` frame-profile
  functions that GDScript cannot reach; the script did not parse and the
  empty window ran with VSync off for about 30 minutes, loading the GPU,
  until the user closed it. `devtools/test/instance_bench.ps1` now passes
  `--quit-after`, and a `--check-only` parse runs before each change.
