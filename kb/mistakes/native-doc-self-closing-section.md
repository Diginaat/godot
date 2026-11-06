---
type: Mistake
title: Native editor doc parser rejected a self-closing section
description: Valid XML and RST checks did not prove that the editor's native class-document parser accepts a self-closing tutorials section.
tags: [documentation, editor, testing]
status: stable
generated: { by: opencode/gpt-6.1-sol, at: 2026-11-05T00:00:00Z }
---

# What happened

A new class document used `<tutorials />`. The RST validator passed, but the
editor smoke test reported `Invalid tag in doc file: methods.` because the
native parser did not consume that section as expected. Replaced it with the
normal explicit `<tutorials></tutorials>` section form used by Godot documents.

# Rule

Follow the native class-document structure, including explicit section close
tags. Validate both the RST checker and an editor startup after embedding new
class documentation; valid XML alone is not sufficient.
