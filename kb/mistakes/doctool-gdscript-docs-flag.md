---
type: Mistake
title: Ran doctool with --gdscript-docs from the repository root
description: The flag wrote 567 script doc XML files into the repository root, and doctool also rewrote unrelated class docs.
tags: [mistake, docs, tooling]
status: stable
generated: { by: claude-opus-5-5, at: 2026-11-24T00:00:00Z }
---

# What happened

To check new class docs, `godot --headless --doctool . --gdscript-docs res://`
was run in the repository root. `--gdscript-docs` documents every GDScript
under the path (including `modules/gdscript/tests/`) and wrote 567 XML files
into the root. A second plain `--doctool .` run also reordered and filled
in docs unrelated to the change (PhysX module, DDGI, MultiMesh), because the
tracked docs drift from the code.

# Rule

Validate new docs with `--doctool .` alone, then keep only the hunks for
the change (`git checkout` the other files) and remove nothing but the
files the run created (`git clean -n` first, with a root-only pathspec).
