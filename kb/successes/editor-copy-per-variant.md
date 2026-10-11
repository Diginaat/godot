---
type: Success
title: One editor copy per variant for A/B tests
description: Copying each build's editor exe in bin/ under its own name made A/B tests cost one build per variant instead of one per test.
tags: [success, testing, workflow]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What worked

Early A/B tests stashed the change, rebuilt the baseline, tested, popped
the stash and rebuilt again for every new test (a few minutes per
rebuild). Instead, after each build:

- copy `godot.windows.editor.x86_64.exe` and `.console.exe` in `bin/` to
  `godot_<variant>.windows.editor.x86_64.exe` and the matching
  `.console.exe` (the DLLs next to them are shared);
- give the test runner a build name parameter.

Any test then runs on any variant without a rebuild, and new tests (the
game level, the emissive step) were run on old variants afterwards.

# Reuse

Name the copies after the variant and keep them until the work is merged.
`bin/` is not tracked.
