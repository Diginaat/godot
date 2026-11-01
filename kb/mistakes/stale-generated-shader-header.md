---
type: Mistake
title: A restored shader source didn't rebuild its generated header
description: A shader file restored from a backup copy kept an older timestamp than its .glsl.gen.h, so SCons kept the generated header of the debug version and the build ran stale code.
tags: [mistake, build, shaders]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-01T00:00:00Z }
---

# What happened

- A debug line (paint the resolve's decision red or green) was added to
  `planar_reflections.glsl`, built and looked at. The file was then put
  back from a backup copy with `Copy-Item`, which keeps the backup's
  modification time.
- SCons rebuilt and linked, but the floor stayed green in every run.
  The source had no debug line; the `.glsl.gen.h` (zlib-compressed, so
  grep doesn't see inside it) still did. Time went into suspecting the
  Godot shader cache first.

# Rule

- Restore a file by editing it, or touch it after copying, so its
  modification time is newer than everything generated from it.
- When a shader change doesn't show, check the generated header:
  decompress `_code_compressed` (zlib) and search it, or delete the
  `.gen.h` and rebuild.
