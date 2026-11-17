---
type: Mistake
title: Scripted GUI clicks and a full-screen capture on a desktop in use
description: A click-through of a project manager dialog captured the whole screen, including the maintainer's own editor and project, and picked a tooltip as the target window.
tags: [mistake, testing, editor, gui]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-17T13:50:00+01:00 }
---

# What happened

- A dialog in the project manager was tested with scripted mouse clicks
  and screenshots (a self-contained copy of the editor, its own
  `editor_data`, a scratch project).
- `Get-Process` gives one `MainWindowHandle` per process. Once a tooltip
  was open, that handle was the tooltip, so the window screenshot was
  135 x 34 pixels and the focus step targeted the wrong window.
- The fallback, a capture of the whole primary screen, showed the
  maintainer's own editor with a private project open behind the test
  window. Clicks go to screen coordinates: with the test window not in
  front, they would have landed in that editor.

# Rule

- Before scripted clicks or keys, make sure nothing else the user runs is
  on that screen, or ask. Stop as soon as another application shows up.
- Capture only the test window's rectangle, never the whole screen.
  Find the window by title (`EnumWindows`), not by `MainWindowHandle`.
- Delete any capture that shows something outside the test.
