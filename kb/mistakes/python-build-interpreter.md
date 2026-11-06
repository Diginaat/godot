---
type: Mistake
title: Default Python was not the configured build interpreter
description: A build failed before configuration because PATH selected Python without SCons; use the existing configured interpreter.
tags: [build, windows, tooling]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# What happened

The documented `python -m SCons` command selected an interpreter without SCons.
No engine compilation occurred. A configured interpreter was already installed;
installing packages globally or rerunning complete SDK setup was unnecessary.

# Rule

On Windows, inspect `py -0p` and test the installed interpreters with
`py -<version> -m SCons --version`. Use the configured interpreter while retaining
the normal build flags. Record the actual build command in validation evidence.
Do not change unrelated Python installations or rebuild SDKs to fix PATH choice.
