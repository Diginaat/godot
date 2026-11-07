# Split workspace and Live Runtime view

This build can show the running game and the 2D/3D editor at the same time,
in the central workspace of the editor. This file describes the design, how to
use it, what it costs and what it does not do. Work happens on the
`dev-split-workspace` branch.

| Step | What | State |
| --- | --- | --- |
| 1 | Split workspace: a second main screen panel next to the main screen | Done, built, tested (see Tests) |
| 2 | Live Runtime view: the editor viewport follows the running game | Done, built, tested (see Tests) |

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

The scene tabs sit above the main screen only (in the same column), and the
split panel's tab bar is kept exactly as tall as them
(`EditorMainScreenSplit::set_header_reference()`). So the two panels line up
row by row: scene tabs and the panel's tab, the main screen toolbar (for
example the 3D toolbar) and the docked view's toolbar (for example the Game
toolbar), and both viewports.

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
  panel's role. The split panel's docks stay listed in the title bar too
  (after the main screen buttons, highlighted while shown), so Game is always
  there wherever it is docked.
- With a dock in the split panel, its context menu shows **Vertical Split** and
  **Swap Panels**. The divider between the panels is draggable.
- Each panel has a maximize button (the distraction-free icon) at the right of
  its tab row. The scene tabs' button maximizes the main screen: the docks and
  the split panel are hidden, so only the viewport is left. The split panel's
  button does the same for its view (for example only the Game view). Both use
  distraction-free mode, so the shortcut and the bottom panel behave as
  before. Pressing the button again, picking another main screen, or clicking
  the split dock's title bar entry while the main screen is maximized
  restores both panels.
- Move the docks back to the main screen to close the split.

**Saved state** (`[docks]` in `editor_layout.cfg`): the split panel's docks
(`dock_13`), `dock_workspace_split` (divider offset from the middle, unscaled),
`dock_workspace_split_vertical` and `dock_workspace_split_swapped`. Named editor
layouts store the same keys.

**Changed code.**

| File | Change |
| --- | --- |
| `editor/docks/editor_dock.h/.cpp`, `doc/classes/EditorDock.xml` | New slot `DOCK_SLOT_MAIN_SCREEN_SPLIT` (`DOCK_SLOT_MAX` is now 13) |
| `editor/editor_main_screen.h/.cpp` | `EditorMainScreenSplit` (tab bar height follows the scene tabs, title bar entries, maximize button); main screen grid cell is now the left half |
| `editor/editor_node.h/.cpp` | `DockSplitWorkspace` holding both panels, with the scene tabs in the main screen column; edge drops; theme; distraction-free mode maximizes one side |
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

## Step 2: Live Runtime view

The 2D and 3D editor viewports get a **Live** toggle in their toolbars. Off is
**Standard**: the viewport shows the edited scene, and the existing live edit
(changes in the editor sent to the game) works as before. On is **Live
Runtime**: while the game runs, the viewport shows the running scene: the
player moving, rigid bodies falling, objects spawned and freed at runtime,
nodes shown and hidden. The editor camera stays independent, so the running
game can be watched from any angle next to the Game view.

### Options that were considered

| Option | Accuracy | Cost | Verdict |
| --- | --- | --- | --- |
| Copy rendered frames from the game to the editor | Exact | GPU to CPU readback and a copy over the debugger connection every frame | Rejected (the task forbids it, and it is the most expensive) |
| Send the whole scene (remote tree) every update | Good | Proportional to the scene size per update | Rejected; the remote scene tree is meant for inspection, not streaming |
| A second camera in the game, rendered into a second embedded window | Exact (same simulated world, all effects) | One more render of the game world per frame, in the game | Evaluated, not built (see below) |
| **Stream only what changed, apply it to the editor's own scene** | Positions, rotations, scale, visibility, spawned instances | Proportional to what moved; nothing for static nodes | **Built** |

**Why not the second runtime camera (yet).** It is the only way to get exact
visuals (animated skeletons, particles, material changes, effects). In this
code base it means: the game opens a second native window with a `Camera3D`
that shares the game's `World3D`, follows the editor camera (the camera
override messages already exist), and the editor embeds that window over its
3D viewport. Three things make it a large change rather than a step:
`DisplayServer::embed_process` embeds one window per process (it looks the
window up by process ID), so a second window per process needs a new display
server API on every platform; a native child window covers the editor viewport,
so gizmos, selection and the editor's own overlays can't be drawn on top; and
the game pays for a whole second render (with DDGI, path tracing or DLSS, close
to doubling its GPU time). The streamed approach keeps the editor's gizmos and
tools, costs little, and works for 2D too. The runtime camera stays the
documented upgrade path for exact visuals.

### How it works

Game side, `RuntimeStateObserver` (`scene/debugger/runtime_state_observer.cpp`,
debug builds only, created with the `SceneDebugger`):

- Does nothing until the editor sends `runtime_observer:setup`. Then it runs on
  the scene tree's `process_frame`, at most `update_rate` times per second.
- The observed root is the runtime node instanced from the edited scene (the
  current scene, or a level instanced inside the main scene), so paths match the
  editor's scene tree.
- Each update walks the tree below the root (not into SubViewports) and looks at
  `VisualInstance3D` nodes (meshes, lights, particles, decals, CSG...) for 3D and
  `Node2D` nodes for 2D. It keeps the last sent global transform and visibility
  per node and sends only the ones that changed. Nodes that appeared are sent
  once with their path, class, the scene file of the instance they belong to,
  and their mesh or texture file. Nodes that disappeared are sent as removed IDs.
  When nothing changed, nothing is sent.
- Transforms travel as packed float arrays (12 floats per 3D node, 6 per 2D
  node) with numeric IDs, not as node paths or dictionaries.

Editor side, `LiveRuntimeDebugger` / `LiveRuntimePlugin`
(`editor/run/live_runtime_plugin.cpp`):

- Asks the first running instance for the stream only while the mode is Live
  and the 3D or 2D editor is visible (in the main screen, the split panel or a
  floating window). Hiding both editors stops the stream in the game.
- Applies transforms and visibility to the rendering server instances of the
  edited scene's nodes (`instance_set_transform`, `canvas_item_set_transform`).
  The nodes' properties are not touched, so the scene is never modified, the
  undo history is untouched and saving during play saves the edited values.
- Nodes that only exist in the game are shown as ghosts: an instance of the
  scene file they come from (for example a spawned enemy), or a mesh or sprite
  made from their own resource file. Ghosts live in internal nodes of the
  editor's scene root, outside the edited scene, with processing disabled. A
  ghost is freed when the game frees the last of its nodes.
- On Stop, mode off, a scene tab change or when the views are hidden, every
  node is restored from its own properties and all ghosts are freed.
- The toolbar button's tooltip shows the state: the number of followed nodes,
  spawned instances and nodes that could not be shown.

### Limitations

- Shown: transforms (position, rotation, scale), visibility, nodes appearing
  and disappearing, spawned scene instances. Not shown: skeleton poses and
  blend shapes, material or mesh changes made by scripts, `MultiMesh` instance
  changes, `Control` nodes, shader parameters. Particles and animated materials
  play in the editor as they do in Standard mode, not in sync with the game.
- Nodes created by code without a scene file or a mesh/texture file can't be
  shown (counted as "not shown" in the tooltip).
- Gizmos and the selection box stay at the edited positions while the meshes
  move.
- Editing a node while Live is on moves it back to its edited position until
  the game reports it again (static nodes are not sent again).
- With several running instances, the first one is shown.
- Ghost scenes are instanced in the editor, so `@tool` scripts in them run
  their `_ready` (processing is disabled).

### Performance

- Game: one tree walk per update (`update_rate`, default 30 Hz) over the
  observed scene, comparing transforms; data only for changed nodes. With the
  views hidden or the mode off, the game does no work at all (it isn't even
  connected to `process_frame`).
- Network: about 52 bytes per moved 3D node per update (ID + 12 floats);
  static scenes send nothing. New nodes are announced at most 2048 per message.
- Editor: one rendering server call per changed node, no scene tree changes.
  2D recomputes the local transforms of all followed 2D nodes when any of them
  moved (a parent's move changes its children).
- Settings: `run/live_runtime/update_rate` (1–120 Hz, default 30) and
  `run/live_runtime/max_nodes` (default 20000).

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
| Live, 3D: game in the split panel, 3D editor in the main screen | Pass: the 3D view shows the moving player, the fallen ball and the spawned crates (12, then 18 ghosts as the spawner adds them) |
| Live, 3D: edited scene untouched | Pass: `Player.position` and `Ball.position` keep their edited values during and after play |
| Live: Stop, play again, hide the 3D editor, show it again | Pass: ghosts freed on Stop and on hide (tooltip: paused), full resync on show |
| Live, 2D: rotating parent with a child sprite, despawning bullets | Pass: the 2D view matches the Game view (parent rotation, child offset); bullet ghosts stay at about 6, freed with the game's nodes |
| Live, 2D: Stop | Pass: every node back at its edited transform |
| Standard mode (Live off) | Pass: no ghosts, no messages, no errors |
| Repository smoke tests (`devtools\test\smoke.ps1`) | Pass: PhysX game and editor, path tracer, DDGI |

Not tested here: macOS and Linux embedding (the split only moves the
`EmbeddedProcess` control, so it uses the same code as before), touch input,
and mouse capture inside the embedded game by hand (the Game view's input
handling is unchanged).
