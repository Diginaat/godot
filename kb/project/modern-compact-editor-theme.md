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

## Header, gray variant and inactive tabs (2026-11-07)

Both compact presets use a darker header (`#121416` in Modern Compact).
**Modern Compact Gray** is selectable under Editor Settings > Interface > Theme
> Color Preset. It shares compact geometry, smooth opaque outlines and icon
saturation, but uses a neutral `#232323` base and grayscale surface tokens.
Semantic icon colors and blue focus/selection accents remain available.

Inactive dock and scene tabs use a slightly darker base-color surface, brighter
text/icons and a four-logical-pixel gap with Compact spacing, without an outline (including hover and disabled
states). Selected tabs keep a lighter surface, smooth one-logical-pixel outline,
and brighter text. Hover sits between
inactive and active surfaces; neither variant adds decorative accent lines.
All four tab corners are rounded. The tab background is inset two logical pixels
above and below, creating a floating browser-tab appearance without changing
layout or state-dependent content margins. Inner tab variants share this treatment.
Scene/dock and inner tabs add 2px vertical content padding on each side (4px
total height at 100% scale) so floating pills do not look too narrow. All states
share the increased padding. Main-screen switch padding remains unchanged.
Context menus (including submenus), popup panels and tooltips also use the shared
six-logical-pixel corner radius, full antialiasing and opaque 1px outlines in both
compact presets. Other presets and OS-managed native menus/dialogs are unchanged.
Compact popups reserve a 2px fully transparent gutter through the native popup
controls' existing shadow-inset mechanism (shadow color is fully transparent).
This keeps rounded antialiasing away from the native window boundary without
changing menu fill opacity, corner radius, item spacing, or control behavior.
An earlier readback only checked corner pixels; its straight window-edge pixels
were still opaque. The regression check now requires the complete outer pixel
perimeter to be fully transparent. Desktop fringe appearance needs fresh visual
confirmation for this gutter revision.
For native popup corners, editor/Project Manager startup enables layered-window
composition independently of `display/window/per_pixel_transparency/allowed`.
Otherwise rounded popup images can contain transparent pixels that the native
window still displays as black. Game startup and the project-setting default
remain unchanged; no blur, glass effect or external framework is introduced.
Validation passed on Windows OpenGL, Vulkan and D3D12: native transparency is
reported available, popup corner pixels retain alpha, and taller tab padding and
fixed switch positions pass in both compact presets. Project Manager starts
cleanly, and a separate game process retains disabled native transparency for a
project with the default setting. The user confirmed the final desktop appearance,
including popup corners; image readback alone was not treated as that confirmation.
Contrast refinement raises compact text opacity to 0.82 (secondary 0.65), deepens
input fields, lightens elevated surfaces/buttons and strengthens opaque neutral
outlines without increasing their width. Derived surface contrast is 0.26. The
gray variant desaturates the same palette, retaining its neutral surfaces.
Dock tabs and bottom-dock tabs default to **Text and Icon**, matching the existing
desktop main-screen default. Saved user choices are not overwritten; the existing
Android main-screen icon-only default remains unchanged.
The rebuilt Windows editor passed live checks for these defaults, popup style
geometry, actual transparent popup corners, brighter text, and tab stability in
both compact presets. Actual PopupMenu screenshots were inspected; native OS
menus/dialogs and other DPI scales were not visually validated.
Changes stay in editor settings and shared theme generation, plus an editor-only
startup allowance for native popup alpha composition. No native control logic
or project runtime UI changes.

The 2D/3D/Script/Game/Asset Store switches reserve identical content margins in
normal, hovered, selected and disabled states. With Compact spacing these are
6px horizontal and 3px vertical, with a consistent 4px gap between switches and
4px between icon and text, all editor-scale aware. Previously selected/hovered
styles reserved less padding than the empty normal style, moving content when
switching state.

Padding regression proof: the previous build failed all four content-margin
comparisons for selected/hovered/disabled main-screen states. After the fix,
both compact presets passed equal-margin checks and live selection switching
through the first three main-screen tabs without changing any tab rectangle.
The full preset-switching check completed with its explicit PASS marker and no
script errors on the rebuilt Windows production editor (OpenGL, 100% scale).

Validation: Windows production editor build passed. A temporary editor-plugin
fixture ran live switching through Modern Compact, Modern Compact Gray, Default
and back on OpenGL Compatibility at 100% editor scale. It checked header
brightness, inactive/active tab contrast, scaled tab spacing and outline width,
opaque antialiased corners, neutral gray surfaces and preserved blue accent.
All checks passed without script errors; captured editor screenshots for both
compact presets were inspected. Other DPI scales and native OS title-bar styling
were not visually validated in this pass. No physics/path-tracer smoke claim.

## Popup transparency (2026-11-07)

Integration into `dev` preserves both compact presets and their transparent
gutter. Startup already enabled layered windows here; the additional code change
is the Windows GDI erase fix. Measurements below were taken on the source branch
before cherry-picking, with its six-pixel corners and no gutter.

Editor and project-manager startup now allow per-pixel window transparency
independently of the project's setting. PopupMenu already requests a transparent
viewport/window; disabling layered windows prevented native alpha composition.
Modern Compact menus use the configured corner radius rather than forcing square
corners, with no theme shadow. Runtime project settings retain their defaults.
Validation: production editor build passed with Python 3.14/SCons, `-j10`;
project-manager startup passed on Vulkan and D3D12 (`--quit-after 10`, exit 0).
Follow-up: viewport readback was transparent black, but native Vulkan popup
corners still added RGB (25, 27, 30)/255 to the desktop. `WM_ERASEBKGND` painted
the editor's early-clear color into the GDI backing surface. Clearing that
surface to black at runtime made zero-alpha corner pixels match the background
exactly; disabling DWM borders, rounding, or backdrop did not. D3D12 did not
show the additive rectangle. Transparent windows now use a black GDI erase
brush rather than the opaque editor early-clear color. Production rebuild passed.
Native Vulkan and D3D12 checks each found 24 zero-alpha corner pixels and zero
desktop-background mismatches (RTX 3060, radius 6, 100% scale). The user confirmed
the fix works.

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
outline. Inactive and hovered tabs remain borderless. A requested 2px neutral
top-marker trial was reverted after the user rejected its appearance; do not
restore that marker without new visual approval. Fixed state padding remains.

The outline treatment follows current native-tool practice: component and
surface borders use low-contrast neutral tokens, while focus borders remain a
separate accent state. This matches the separation documented by VS Code's
`surface.border`, `button.border`, `input.border`, and `focusBorder` tokens and
JetBrains theme support for centralized control borders and insets.[^vscode-theme][^jetbrains-theme]

The appearance is owned by the shared editor theme generator. An editor-only
startup flag permits native popup alpha composition. Neither changes `Control`
behavior, dock layout, runtime project theme, or editor plugin API.
Godot's Modern, Classic, other color presets, spacing presets, custom themes,
editor scaling, and touchscreen overrides remain available. Existing editor
settings may retain their saved preset; select **Modern Compact** and
**Compact** under Editor Settings > Interface > Theme to opt in.

Decorative accent borders and active-tab indicator lines are intentionally
absent. Blue remains limited to selection, focus, pressed, and other interactive
states.

[^vscode-theme]: [Visual Studio Code Theme Color reference](https://code.visualstudio.com/api/references/theme-color)
[^jetbrains-theme]: [JetBrains IntelliJ Platform: Getting Started with Themes](https://plugins.jetbrains.com/docs/intellij/themes-getting-started.html)
