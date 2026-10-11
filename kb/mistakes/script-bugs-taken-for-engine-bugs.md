---
type: Mistake
title: Test script bugs looked like engine bugs
description: A GDScript parse error gave a gray window, and a hash test hashed new Image objects, so it always reported a change.
tags: [mistake, testing, gdscript]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

- A parse error in the test project's script (`.hash()` on a
  PackedByteArray) left the window gray. It looked like a rendering
  failure; the user reported it as one.
- A "did the probes change" test hashed newly created `Image` objects,
  not their bytes, so it always said "changed".

# Rule

- The test runner kills the run on `Parse Error`, `Failed to load
  script` or `SCRIPT ERROR` and says so.
- Check a new test on a case with a known answer before using it. For
  example, an unchanged scene must print "no change".
