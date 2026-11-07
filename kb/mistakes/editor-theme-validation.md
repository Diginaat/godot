---
type: Mistake
title: Query the generated editor theme explicitly
description: A Control's local theme may be null even when its inherited editor theme is active.
tags: [editor, theme, testing]
status: stable
generated: { by: openai/gpt-6.1-sol, at: 2026-11-07T00:00:00Z }
---

# Query the generated editor theme explicitly

A temporary editor-plugin theme check called
`EditorInterface.get_base_control().get_theme()` and attempted to inspect its
styleboxes. The control had no local Theme resource, so the test script failed
on a null value; this was not an editor theme-generation failure.

Use `EditorInterface.get_editor_theme()` to inspect the actual generated editor
theme. Scan smoke output for `SCRIPT ERROR` as well as checking process exit:
the bounded editor run can exit successfully after a plugin script error.

For a live main-screen switching test, an unconditional
`RenderingServer.frame_post_draw` wait stalled in an idle workspace. The bounded
editor exited with code zero before the test printed its completion marker.
Do not depend on an unsolicited rendering signal for a style-only assertion;
require the final explicit PASS marker as well as a clean exit and error scan.

Passing style assertions is not visual acceptance: a requested neutral 2px
active-tab marker passed the technical checks, but the user rejected its
appearance. Revert that trial alone, preserving other approved theme changes.

Popup image readback showed transparent rounded corners, but the user still saw
black triangles on screen. The editor inherited the project's disabled native
per-pixel-transparency setting. Image alpha does not prove desktop composition;
also check `DisplayServer.is_window_transparency_available()` and the visible
native popup. Keep any startup correction editor-only, not a game-setting change.

A later corner-fringe report required checking the complete native popup
perimeter, not only `(0, 0)`: with no inset, straight edges reached the window
boundary and their AA fringe was clipped. A compact-only transparent gutter can
use the existing popup shadow-inset mechanism without changing control logic.
Require zero alpha on the full outer perimeter, and still obtain desktop visual
confirmation rather than equating that check with compositor correctness.

On hosts that disable local PowerShell scripts, invoke the approved test runner
with an explicit process-scoped execution policy rather than changing the
machine/user policy. The process still needs the expected completion marker and
an error scan, not merely a zero exit code.
