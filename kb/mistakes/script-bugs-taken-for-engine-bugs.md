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
- A native-culling test's conditional expression produced an untyped `Array`
  for an `Array[int]` destination. The fixture aborted after allocating test
  RIDs, and the frame-limit exit returned 0. The runner correctly rejected
  `SCRIPT ERROR`, leaked RIDs and missing success marker. Use an explicitly
  typed array and `assign()`; corrected kernel/raster tests then passed.
- Resident CLOD shadow-stat coverage initially swapped Viewport.get_render_info
  enum arguments. GDScript rejected the signature; the runner caught parse
  errors despite the engine's frame-limit exit 0. Corrected argument order;
  shadow receiver and native raster tests then passed on Vulkan and D3D12.

# Rule

- The test runner kills the run on `Parse Error`, `Failed to load
  script` or `SCRIPT ERROR` and says so.
- Check a new test on a case with a known answer before using it. For
  example, an unchanged scene must print "no change".
