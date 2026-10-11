---
type: Mistake
title: Quoting broke generated scripts and edits
description: Backslashes and quotes inside code embedded in another language's string broke scripts several times.
tags: [mistake, tooling, scripting]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# What happened

- An inline Python analysis inside a PowerShell script used a raw string
  ending in a backslash (`r'...\shots\'`). Python doesn't allow that,
  so the analysis of a whole measurement run failed and had to be redone.
- A Python edit script in a bash heredoc had to match C++ code containing
  `"\n"`. The escapes changed on the way and the match failed. The
  script's own check caught it, but the shader half of the edit was
  already written.
- A bash command writing several kb files at once failed on an
  apostrophe inside the text.

# Rule

- Write anything longer than a line (analysis, multi-file edits, docs) to
  a file with the editor tool, then run it. Don't embed code or prose in
  another language's string.
- Use forward slashes or `os.path.join` for paths in Python.
- Make edit scripts all-or-nothing: check every match first, then write.
