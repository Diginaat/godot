---
type: Finding
title: Automatic reflections leaked an instance every frame on the dummy renderer
description: Headless runs created a new reflection probe instance per frame; keyed creation on the instance instead of the probe RID.
tags: [ddgi, reflections, headless, leak]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-24T00:00:00Z }
sources:
  - { id: fix, title: "fix(ddgi): create the auto reflection probe once on the dummy renderer", ref: "commit 9c13aba514" }
---

# What happened

A C# project run with `--headless` (dummy renderer) and a `DDGIVolume`
with automatic reflections printed at exit:
`RID allocations of type RendererSceneCull::Instance leaked` (5,726 after
about 10 seconds).

`DDGIAutoReflections::_set_probe()` created the probe when `probe` was
null. On the dummy renderer `reflection_probe_create()` returns an empty
RID, so every frame created a new instance and lost the old one.

# Fix

Creation is keyed on `probe_instance`, which is always valid, and the
disable branch frees `probe` only when it is valid[^fix].

Verified on Linux (.NET editor, headless, 600 frames): no leak lines;
the sandbox controller suite (54 checks) and the smoke tests pass.

# Rule

On the dummy renderer, `*_create()` of storage resources can return an
empty RID. Never use that RID as the "already created" flag; use the
scenario instance or a bool.

[^fix]: commit 9c13aba514
