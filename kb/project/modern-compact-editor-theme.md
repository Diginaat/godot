---
type: Decision
title: Modern compact editor theme
description: Native editor theme preset using neutral charcoal surfaces, restrained blue interaction states, and Godot's compact spacing.
tags: [editor, ui, theme]
status: stable
generated: { by: openai/gpt-5.6-sol, at: 2026-11-06T00:00:00Z }
sources:
  - id: vscode-theme
    title: Visual Studio Code Theme Color reference
    url: https://code.visualstudio.com/api/references/theme-color
  - id: jetbrains-theme
    title: "JetBrains IntelliJ Platform: Getting Started with Themes"
    url: https://plugins.jetbrains.com/docs/intellij/themes-getting-started.html
---

# Modern compact editor theme

## Typography

Update (2026-11-06): the user chose **Atkinson Hyperlegible Next** 2.66
(Regular/Bold, OFL 1.1, `thirdparty/fonts/AtkinsonHyperlegibleNext_*.woff2`)
as the editor UI font. It has 392 glyphs: Latin and Latin Extended only, no
Cyrillic, almost no Greek, no arrows. Inter Regular/Bold therefore stays
embedded as the first fallback in `editor/themes/editor_fonts.cpp`, ahead of
the Noto and Droid fallbacks. Its x-height is 0.496 em, smaller than
Inter's, so text looks slightly smaller at the same size setting. JetBrains
Mono stays the code font. The paragraphs below record the earlier decision.

Retain the existing embedded Inter Regular/Bold for UI and JetBrains Mono
Regular for code (`editor/themes/editor_fonts.cpp`). Both are already wired
into editor font generation with international fallbacks and user overrides.
No additional font assets or runtime dependency are needed.

Inter's text designs use a tall x-height and offer disambiguation and tabular
number features; JetBrains Mono emphasizes increased lowercase height and
distinct code symbols. Both font families use SIL OFL 1.1.
Research sources: [Inter](https://rsms.me/inter/),
[Inter license](https://github.com/rsms/inter/blob/master/LICENSE.txt), and
[JetBrains Mono](https://github.com/JetBrains/JetBrainsMono).
Alternatives reviewed: Source Sans 3 (UI-focused) and Cascadia Code/Mono
(coding-focused). Retaining the embedded pair avoids unnecessary packaging and
upstream merge changes. This is a design recommendation, not a measured claim
of superior readability in this editor.

Ubuntu was reviewed as a reference desktop (2026-11-06). Ubuntu 24.04 uses
Ubuntu Sans 11 for the interface and Ubuntu Sans Mono for monospace text,
replacing the older Ubuntu and Ubuntu Mono families. Ubuntu Sans is
variable, but its licence is the Ubuntu Font Licence 1.0, not OFL. That
licence is copyleft for derived fonts and has its own naming terms, so it is
not bundled; users can load it with `interface/editor/fonts/main_font`.
GNOME 48 moved its default UI font to Adwaita Sans, which is Inter, and
built Adwaita Mono from Iosevka. That supports keeping Inter as the default.
Other OFL options for a later font setting: Atkinson Hyperlegible Next
(2025, seven weights, made for low-vision readers), IBM Plex Sans (dense
tables and panels) and Noto Sans (widest script coverage).
Sources: [Ubuntu Sans fonts](https://github.com/canonical/Ubuntu-Sans-fonts),
[Ubuntu typeface](https://en.wikipedia.org/wiki/Ubuntu_(typeface)),
[Introducing Adwaita Fonts](https://blogs.gnome.org/monster/?p=33),
[Atkinson Hyperlegible](https://en.wikipedia.org/wiki/Atkinson_Hyperlegible).

The fork defaults new editor settings to the native **Modern Compact** color
preset and existing **Compact** spacing preset. Modern Compact uses a neutral
charcoal base (`#202328`), restrained blue accent (`#4d88d8`), low surface
contrast, native icons at 1.25 saturation, six-pixel corner radius, subtle tree
relationship guides, and one-logical-pixel neutral outlines. Existing semantic
SVG shapes and colors remain intact for recognition and plugin compatibility.
Blue remains reserved for checked, selected, focused, pressed, and similar
interaction states.

Rounded outlines use opaque neutral border colors, a one-pixel antialiasing
feather and twelve segments per corner. The earlier half-pixel feather did not
resolve the user's reported bright corner pixels. Opaque colors avoid mixing
transparent white border RGB into opaque fill during inner-edge interpolation;
the revised visual result still needs confirmation.

The user confirmed the opaque-border/full-AA revision resolves the corner
artifact. Subsequent Avalonia-inspired refinement uses opaque charcoal popup
and input surfaces, lighter neutral buttons and hover states, darker pressed
buttons, muted-blue flat-button/tree selections, and no button shadow. Native
Godot controls and the approved six-pixel corners remain in use.

Selected tabs use a raised neutral surface, brighter content, and a subtle 1px
outline. Hovered tabs use a smaller neutral surface shift. Selection never adds
a decorative top or bottom accent line.

The outline treatment follows current native-tool practice: component and
surface borders use low-contrast neutral tokens, while focus borders remain a
separate accent state. This matches the separation documented by VS Code's
`surface.border`, `button.border`, `input.border`, and `focusBorder` tokens and
JetBrains theme support for centralized control borders and insets.[^vscode-theme][^jetbrains-theme]

The implementation stays inside the shared editor theme generator. It changes
no `Control` behavior, dock layout, runtime project theme, or editor plugin API.
Godot's Modern, Classic, other color presets, spacing presets, custom themes,
editor scaling, and touchscreen overrides remain available. Existing editor
settings may retain their saved preset; select **Modern Compact** and
**Compact** under Editor Settings > Interface > Theme to opt in.

Decorative accent borders and active-tab lines are intentionally absent. Blue
remains limited to selection, focus, pressed, and other interactive states.

[^vscode-theme]: [Visual Studio Code Theme Color reference](https://code.visualstudio.com/api/references/theme-color)
[^jetbrains-theme]: [JetBrains IntelliJ Platform: Getting Started with Themes](https://plugins.jetbrains.com/docs/intellij/themes-getting-started.html)
