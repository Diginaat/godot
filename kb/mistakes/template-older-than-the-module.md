---
type: Mistake
title: The export template was built before the new module existed
description: An exported game failed to parse its script ("Could not find type LevelStreamer3D") and hung until the timeout, because the release template was built minutes before the module's config.py was written.
tags: [mistake, export, build, testing]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What happened

The release template for the level streaming acceptance test was built
early, while the module's sources were still being written. The editor
was built afterwards and had the module, so importing, exporting and an
editor-binary run all worked. The exported game, run with that template,
reported `Parse Error: Could not find type "LevelStreamer3D"` and never
quit. Every run hit the 150 s timeout, and the first look was at the
exported files instead of the template.

# Rule

- Build the template after the last change to a module's `config.py`,
  `SCsub` or registered classes. A feature's acceptance build is the
  editor and the template from the same tree.
- Before an exported run, check that the template has the classes:
  `strings <template> | grep -c <ClassName>`.
- A test driver should quit on its own when its script fails to load (a
  hard timeout in the runner, and a check of the log for `SCRIPT ERROR`),
  so a missing class fails fast. See
  [test scene without a hard quit](/mistakes/test-scene-without-hard-quit.md).
