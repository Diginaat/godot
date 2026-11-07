# Split workspace and Live Runtime view

This build can show the running game and the 2D/3D editor at the same time,
in the central workspace of the editor. This file describes the design, how to
use it, what it costs and what it does not do. Work happens on the
`dev-split-workspace` branch.

| Step | What | State |
| --- | --- | --- |
| 1 | Split workspace: a second main screen panel next to the main screen | Done, built, tested (see Tests) |
| 2 | Live Runtime view: the editor viewport follows the running game | See below |

## Source investigation (what the branch already had)

- Every main screen (2D, 3D, Script, Game, Asset Store) is an `EditorDock` in
  `EditorMainScreen`, which is a `DockTabContainer` for the slot
  `DOCK_SLOT_MAIN_SCREEN` (`editor/editor_main_screen.cpp`). Only one of them is
  visible at a time.
- The dock system (`editor/docks/editor_dock_manager.cpp`) already moves docks
  between slots, floats them in windows, saves the layout per project
  (`.godot/editor/editor_layout.cfg`) and draws the Dock Position grid in the
  dock context menu (`DockSlotGrid`).
- The Game view (`editor/run/game_view_plugin.cpp`) is an `EditorDock` that
  embeds the game's own window through `EmbeddedProcess`
  (`editor/run/embedded_process.cpp`, `DisplayServer::embed_process`). The game
  stays a separate process. The embedded window follows the screen rectangle of
  the `EmbeddedProcess` control, wherever that control is.
- The 3D and 2D editors already allow floating (`DOCK_LAYOUT_FLOATING`); the Game
  dock floats with its own window wrapper (Embed > Make Floating on Play).
- The reference addon (game-viewport-3d) moves the `EmbeddedProcess` control into
  a 3D viewport pane by walking editor internals. That is the same idea, but it
  depends on class names and node layout and has to fight the Game dock's focus
  handling.

## Step 1: split workspace

**Design.** One new dock slot, `EditorDock::DOCK_SLOT_MAIN_SCREEN_SPLIT`, held
by `EditorMainScreenSplit` (a `DockTabContainer` with the main screen layout).
`EditorNode` puts it next to `EditorMainScreen` in a `DockSplitContainer`
(`DockSplitWorkspace`). Any dock that can be a main screen (Game, 2D, 3D,
Script, Asset Store, plugin main screens) can be moved there. The panel is
hidden while it holds no dock, so the editor looks and behaves as before until
someone uses it.

Because it is a normal dock slot, everything the dock system does works for it
without new code: drag and drop of tabs, the Dock Position grid, the docks menu,
shortcuts, closing and reopening, and saving the layout. The game keeps running
in its own process; only the place of the `EmbeddedProcess` control changes.

**How to use it.**

- Right-click a main screen tab (for example Game) or open the dock menu. In the
  Dock Position grid, the top centre is now two cells: left is the main screen,
  right is the split panel. Click the right one.
- Or drag a main screen tab onto an edge of the main screen. The edge sets the
  orientation: left/right gives side by side, top/bottom gives stacked, and
  left/top puts the split panel first.
- Each panel has its own tabs, so its role (Game, 2D, 3D, Script) is chosen by
  its selected tab. The main screen buttons in the title bar select the main
  panel's role.
- With a dock in the split panel, its context menu shows **Vertical Split** and
  **Swap Panels**. The divider between the panels is draggable.
- Move the docks back to the main screen to close the split.

**Saved state** (`[docks]` in `editor_layout.cfg`): the split panel's docks
(`dock_13`), `dock_workspace_split` (divider offset from the middle, unscaled),
`dock_workspace_split_vertical` and `dock_workspace_split_swapped`. Named editor
layouts store the same keys.

**Changed code.**

| File | Change |
| --- | --- |
| `editor/docks/editor_dock.h/.cpp`, `doc/classes/EditorDock.xml` | New slot `DOCK_SLOT_MAIN_SCREEN_SPLIT` (`DOCK_SLOT_MAX` is now 13) |
| `editor/editor_main_screen.h/.cpp` | `EditorMainScreenSplit`; main screen grid cell is now the left half |
| `editor/editor_node.h/.cpp` | `DockSplitWorkspace` holding both panels; edge drops; theme |
| `editor/docks/editor_dock_manager.h/.cpp` | Orientation, swap, save and load; Vertical Split and Swap Panels in the context popup |
| `editor/docks/dock_tab_container.cpp` | Dropping on a main screen edge sets the split orientation |
| `editor/editor_interface.cpp` | `set_main_screen_editor()` also finds editors in the split panel |
| `editor/run/game_view_plugin.cpp` | Focus fallback uses the Game dock's own panel, not always the main screen |
| `editor/scene/editor_scene_tabs.cpp` | Scene tab previews check the Game dock's visibility, wherever it is |

**Behaviour notes.**

- The Game dock keeps all its features in the split panel: Play, Pause, Next
  Frame, Stop, speed, input capture, the 2D/3D selection tools, camera override,
  fixed/aspect/stretch sizing and Make Floating on Play.
- Play focuses the Game tab of the panel it is in. With Game in the split panel,
  the main panel keeps showing 2D or 3D, and Stop leaves both as they are.
- Selecting a node switches the panel that holds the matching editor, as before;
  a panel showing the Game view is not switched away (the Game dock doesn't
  allow automatic switching).
- The split panel stays visible in distraction-free mode, because it is part of
  the workspace, not a side dock.
- Floating: 2D, 3D and Script float as before (context menu > Make Floating).
  The Game dock can't float as a dock; its game window floats with the Game
  view's own Embed > Make Floating on Play option.

## Tests

The automated check is an editor plugin in a scratch project (a 3D scene with a
moving player, a falling rigid body and a spawner that adds 30 rigid crates). It
opens the scene, plays, stops and plays again, and saves screenshots of the
editor window and the state of the split.

| Check | Result |
| --- | --- |
| Layout with Game in the split panel is restored on project open | Pass: split visible, Game tab current, halves 785/785 px |
| Play: game embedded in the split panel while the 3D editor is visible | Pass: game at 60 fps in the panel, 3D editor usable next to it |
| Stop and play again (two cycles) | Pass: no errors, Game stays in the split panel |
| Vertical + swapped + offset −150 restored | Pass: split panel on top, 188 px (middle − 150) |
| Default layout, no split | Pass: split panel hidden, editor unchanged |
