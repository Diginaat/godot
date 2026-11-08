# Update log

## 2026-11-08 (rain emission)

* **Implemented**: `WeatherRainEffect.emission`, a self-lit glow of the
  drops in the rain colour (default 0.5), so rain stays visible at night
  and where the refracted background hides it. Checked on Vulkan in a dark
  storm at 0, 0.5 and 1.5. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 8: wind zones)

* **Implemented**: `WindZone3D` (directional, radial, vortex; sphere or
  box; add or replace), zones in `WindServer.sample()` and the wind shader
  include through a float texture, `WindServer.sample_global()`, an
  editor gizmo. 22 checks; GPU matched CPU within 0.031 m/s. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 7: sound)

* **Implemented**: `WeatherSoundEffect` and `WeatherSoundSynth`: rain,
  wind and thunder synthesized in C++ on worker threads (no audio files),
  crossfaded rain, wind by speed, thunder delayed by distance through a
  low-pass and reverb bus. 22 checks. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 6: lightning)

* **Implemented**: `WeatherLightningEffect` (return-stroke flashes lighting
  the scene and the sky, branched bolts, Strike Lightning button) and
  `Weather.lightning_struck`. Fixed: the sun search took internal lights.
  20 checks headless and on Vulkan. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 5: haze and motes)

* **Implemented**: `WeatherHazeEffect` (volumetric ground mist, rain shafts
  and spray in a camera-following `FogVolume`), `WeatherMotesEffect`
  (floating droplets) and `Weather.get_effect()`. 16 checks headless and on
  Vulkan. See [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 4: rain)

* **Implemented**: `WeatherRainEffect` (GPU drops and impacts around the
  camera, collision height field, top-down rain occlusion map) and the
  built-in wetness include and wet surface shader, served from memory and
  left out of exports. Wet ground darkened to 0.77 on Vulkan while the
  ground under a roof stayed dry. See
  [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 3: sky)

* **Implemented**: `WeatherSkyEffect`: drives a weather-ready sky material
  in place, or shows the built-in weather sky (shader kept as a C++ string,
  noise textures built in code) in place of the scene's while it runs.
  Clouds drift with the wind aloft. 17 headless checks and Vulkan
  screenshots. See [World wind and weather core](/project/world-wind.md).

## 2026-11-07 (weather step 2: core)

* **Implemented**: `WeatherProfile`, `WeatherLibrary` (11 built-in types),
  `Weather` node (blending, wind driving, wetness globals, borrowed scene
  values restored before save), `WeatherEffect` resources and
  `WeatherLightingEffect`. 24 headless checks pass. See
  [World wind and weather core](/project/world-wind.md) and `WEATHER.md`.

## 2026-11-07 (weather step 1: wind)

* **Implemented**: `modules/weather/` with the `WindServer` singleton (gusts
  that travel with the wind, same noise in C++ and in the built-in shader
  include `res://.builtin/wind.gdshaderinc`), `WindReceiver3D` (drag on rigid
  bodies), wind in `GrassField3D`'s default material and
  `PhysXCloth3D.use_global_wind`. GPU matched CPU within 0.043 m/s. See
  [World wind](/project/world-wind.md) and `WEATHER.md`.

## 2026-11-07 (sync)

* **Updated**: CUSTOM_BUILD.md sync log: upstream `b47a795aa5`, physx module
  `92da57bda8`. PXTGI needed `node_3d_editor_viewport.h` (master split it out
  of the 3D editor plugin). Smoke: PhysX GPU, path tracer and DDGI pass.
* **Decision**: NVIDIA force-pushed `nvidia-pt-dlss` (`49a825dc52`, same base,
  about 28k changed lines). Not merged: against the old tip it conflicts in
  53 files (130 hunks, mostly the path tracer and the renderer interface,
  where this fork added planar reflections and DDGI). The fork keeps NVIDIA
  `135dff3887`. A later merge should use `135dff3887` as the base
  (`git merge-tree --merge-base=135dff3887`), not the upstream merge base.

## 2026-11-07 (popup corner transparency)

* Follow-up reproduced the remaining rectangle: native Vulkan composition added
  the GDI early-clear RGB behind an already zero-alpha popup texture. A black
  GDI erase experimentally restores exact background pixels; patched
  `WM_ERASEBKGND` for transparent windows. Production rebuild passed; Vulkan and
  D3D12 each verified 24 zero-alpha pixels with zero background mismatches.
  User confirmed the fix works.

* Enabled editor/project-manager per-pixel transparency independently of project
  settings; Modern Compact menus now retain rounded corners without a theme shadow.
  See [theme notes](/project/modern-compact-editor-theme.md). Production build and
  Vulkan/D3D12 project-manager startup passed; user confirmed popup appearance.

## 2026-11-07 (split workspace and Live Runtime view)

* **Implemented**: second main screen panel (`DOCK_SLOT_MAIN_SCREEN_SPLIT`) so
  the Game view and the 2D/3D editor show at once; Live Runtime toggle in the
  2D/3D toolbars that streams changed transforms and visibility from the game
  over the debugger and applies them to rendering server instances, with
  ghosts for spawned instances. Design, evaluation of a runtime camera,
  limits and costs in `SPLIT_WORKSPACE.md`.
* **Validated**: scratch 3D (rigid bodies, spawner) and 2D (parented sprites,
  despawning bullets) scenes, play/stop cycles, hidden views, smoke tests.

## 2026-11-07 (transparent popup perimeter)

* **Refined**: reserve 2px fully transparent padding around compact popup menus,
  panels and tooltips using existing shadow insets, with no visible shadow.
  Rounded antialiasing no longer touches the native window boundary; fill,
  radius and item spacing stay unchanged.
* **Evidence**: prior popup readback had transparent corner pixels but opaque
  straight-edge pixels at the window boundary. Add a full-perimeter alpha check,
  rather than treating one transparent corner pixel as sufficient proof.
* **Validated**: production build and OpenGL/Vulkan/D3D12 checks passed. The
  complete outer popup perimeter now has zero alpha in both compact presets;
  dock defaults, floating tabs and switch stability still pass. Project Manager
  and unchanged game-default checks pass. Desktop visual confirmation pending.

## 2026-11-07 (compact header and gray theme)

* **Implemented**: darker compact header, selectable Modern Compact Gray with
  untinted surfaces, and clearly bounded inactive scene/dock tabs. Shared compact
  geometry and approved opaque antialiased outlines remain unchanged.
* **Padding correction**: main-screen switches share identical normal/hover/
  selected/disabled margins and consistent gaps; selection no longer shifts content.
  Verified failing margin checks before the patch, then passing equal-margin and
  live fixed-tab-rectangle checks in both compact presets after rebuilding.
* **Inactive tabs**: removed outlines from unselected/hovered/disabled tabs,
  including inner tab variants; selected-tab outlines and fixed spacing remain.
  Rebuilt and passed live checks for zero inactive borders on all four sides,
  retained selected borders, and unchanged main-screen switch rectangles.
* **Active marker**: user requested a neutral 2px top edge on selected tabs;
  shared treatment covers scene/dock, main-screen and bottom-panel tabs without
  adding colored toolbar lines or changing content margins.
  Production build and live checks passed: neutral 2px markers, borderless
  inactive states, fixed switch rectangles, both compact presets and Default.
  **Reverted after visual feedback**: user rejected the marker. Restore prior
  selected-tab styling only; keep borderless inactive tabs and fixed padding.
* **Floating tabs**: slightly darker inactive fill, four rounded corners and a
  2px vertical drawing inset with 4px gaps at Compact spacing. Layout/content
  margins remain unchanged; inactive tabs stay borderless, with no top marker.
  Production build and live checks passed for both compact presets and Default;
  verified rounded corners, insets, darker inactive fill, borderless inactive
  states and stable switch rectangles. Captured editor screenshot inspected.
* **Rounded popups**: context menus/submenus, popup panels and tooltips share
  compact themes' smooth 6px corners and opaque 1px outlines. Other presets and
  native OS window decorations remain unchanged.
* **Contrast**: brighter text, darker inputs, lighter elevated/button surfaces
  and slightly stronger opaque neutral outlines; width and density unchanged.
* **Dock defaults**: Text and Icon for dock and bottom-dock tabs, matching the
  desktop main-screen default. Saved preferences and Android's main-screen
  default are preserved.
* **Tab height**: scene/dock and inner tabs gain 2px padding above/below;
  floating insets, widths and main-screen switch padding stay unchanged.
* **Black popup corners**: enable layered windows only for editor/Project Manager
  startup, independent of the game's transparency setting. Transparent popup
  image corners alone did not prove native composition was enabled.
* **Validated**: production build; live compact/gray/Default checks passed on
  OpenGL, Vulkan and D3D12, including native transparency availability, popup
  alpha, taller tab padding and stable switch rectangles. Project Manager startup
  passed; a separate game run confirmed transparency remains disabled by default.
  User confirmed final desktop appearance, including popup corners.
* **Validated**: rebuilt production editor; both compact presets and Default
  passed live checks, including dock revert defaults, increased text contrast,
  popup corner geometry/transparent outer pixels, inactive tabs and stable main
  switches. Rendered popup screenshots for both compact presets inspected.
* **Scope**: native editor theme only; no control or runtime UI changes.
* **Validated**: Windows production build; live Modern Compact / Gray / Default
  switching and generated style assertions passed on OpenGL Compatibility at
  100% scale. Both compact editor screenshots inspected. Other scales and
  renderer/physics smoke tests were not run for this theme-only refinement.
* **Test harness correction**: use `EditorInterface.get_editor_theme()` rather
  than a Control's local (possibly null) theme; record script errors separately
  from the bounded editor process's exit status.

## 2026-11-07 (MultiMesh HZB occlusion and the HZB bench)

* **Implemented**: `rendering/geometry/multimesh_gpu_culling/occlusion`
  (D024): instance HZB test after the depth pre-pass, colour passes only;
  shared HZB setup/build; statistic `hzb_instances_occluded`; always-on stage
  timestamps while GPU geometry is active.
* **Measured**: `hzb_bench` city scene, four modes, Vulkan and D3D12: instance
  occlusion -0.4 to -0.6 ms opaque; CLOD two-pass a net +0.2-0.3 ms here.
* **Mistakes**: D3D12 buffer copy then compute write; a bench whose shared
  mesh and material batched every CLOD object out of CLOD (now self-checked).

## 2026-11-06 (CLOD HZB occlusion under jitter)

* **Fixed**: HZB occlusion no longer switches off under TAA, FSR2 or DLSS
  jitter; the test already used the jittered projection (D023).
* **Validated**: `hzb_two_pass` TAA and FSR2 cases (bit-equal images, groups
  occluded), forest TAA A/B 0 px, on Vulkan and D3D12.

## 2026-11-06 (two-pass CLOD HZB occlusion)

* **Implemented**: `rendering/geometry/cluster_lod/occlusion_two_pass`: split,
  test and phase-2 depth with a per-viewport group history, after a reference
  engine's culling (D023); four new statistics words.
* **Validated**: `hzb` kernel fixture (two-pass rounds), new `hzb_two_pass`
  scene fixture on Vulkan and D3D12, full large_world suites and smoke.

## 2026-11-06 (GrassField3D level of detail rings)

* **Implemented**: `GrassField3D.inner_radius`, `inner_fade`, `edge_dither`:
  fields with the same seed hand each blade over between a detailed near
  ring and a card ring (D022).
* **Validated**: `grass_field` fixture LOD ring phase on Vulkan and D3D12
  (2551 + 3447 = 5998 blades, same origins as one field).
* **Measured**: grass test scene at 1080p 10.2 to 7.6 ms GPU.

## 2026-11-06 (paged-out cluster indices, backup before opening)

* **Implemented**: `MeshClusterData.save_pages(path, keep_indices)` and
  `are_indices_paged()`; with `keep_indices` false the resource holds only the
  roots' indices and the renderer reads the rest from the page file, or the
  whole file without streaming (D020). Scene import uses it.
* **Implemented**: the project manager requires a backup before this build
  opens a project the first time, showing project size and free disk space
  (D021).
* **Validated**: `import_paged` extended (paged-out record checks, file-read
  resident copy, missing file with streaming on and off) and all 16
  large_world fixtures on Vulkan and D3D12. The backup dialog builds; it was
  not clicked through yet.

## 2026-11-06 (CLOD page files from scene import)

* **Implemented**: import option `meshes/cluster_lod_page_files`; scene import
  writes one `.clodpages` file per cooked mesh into the imported folder before
  saving and lists it as a generated file, so export takes it along (D019).
  Idea from CRYENGINE 5.7's split of a mesh into a header and a stream-only
  geometry file (ideas only, no code).
* **Validated**: new `import_paged` fixture and the full large_world suite on
  Vulkan and D3D12; standard smoke passes.

## 2026-11-06 (modern compact editor theme)

* **Font changed**: editor UI font is now Atkinson Hyperlegible Next 2.66
  (OFL), with Inter kept as first fallback for Greek, Cyrillic and symbols.
  Ubuntu Sans was reviewed and not bundled (Ubuntu Font Licence, not OFL).
* **Typography research**: recommend existing embedded Inter UI and JetBrains
  Mono code fonts; source confirms both already implemented, with user overrides
  and international fallbacks. No font replacement required.
* **Refined**: user-approved opaque antialiased outlines; Avalonia-inspired
  opaque surface hierarchy, neutral hover/pressed buttons and muted-blue selection.
  Icon saturation increased to 1.25 following visual feedback.
* **Implemented**: native Modern Compact editor color preset and Compact default
  spacing, using neutral charcoal surfaces, restrained blue interaction states,
  colored native icons, small corner radii, subtle tree hierarchy guides and
  low-contrast one-logical-pixel neutral outlines. Selected tabs use a raised
  neutral surface and outline rather than decorative accent lines.
* **Scope**: shared editor theme settings and generation only; no editor control,
  layout, runtime project theme or plugin behavior changes.

## 2026-11-06 (GPU voxel levels for the far field)

* **Implemented**: `VoxelFieldData`, `VoxelProxyBuilder.build_field` and
  `VoxelField3D`, voxel levels with per-brick level selection in a
  compute pass that fills an indirect MultiMesh (D018). Splats are round discs,
  for the static proxies too. The valley flies with GPU levels (64 voxels,
  2 px).
* **Validated**: new `voxel_field` fixture (GPU selection exactly matches the
  CPU at 300-4000 m and orthogonal, frustum, cap, coverage and colour) and the
  full large_world suite on Vulkan and D3D12. Smoke passes.
* **Measured**: image error against the source meshes 3.69 against 5.13 for
  the static proxies; +0.71 ms GPU in the widest valley view. The first bench
  loop measured static in both modes (args dropped by `powershell -File`;
  mistake updated).

## 2026-11-06 (CLOD pages from disk)

* **Implemented**: `MeshClusterData.save_pages` / `clear_pages` /
  `get_page_file` write and record a checked `.clodpages` file; CLOD streaming
  then holds only root indices and reads other groups on worker threads,
  dropping reads of released surfaces (D017).
* **Validated**: new `streaming_disk` fixture (disk convergence, pool rebuild,
  damaged and stale files, free with reads in flight) on Vulkan/D3D12.
  Found and fixed: finished reads were not collected once no streamed surface
  was left.

## 2026-11-06 (GPU grass, far-field cross-fade)

* **Implemented**: `GrassField3D`, GPU-generated grass in a compute-filled
  indirect MultiMesh (hashed blades, height and density grids, frustum-culled
  patches, continuous fade); the valley's meadows use it (key B,
  `--gpu-grass`). The far-field switch cross-fades over 8 m with Godot's
  visibility-range fade (`--far-hard-cut` keeps the old switch). The streaming
  fixture checks the streaming-memory monitor.
* **Validated**: new `grass_field` fixture (exact blade count, heights exact on
  a non-linear triangulated grid, density mask, frustum bounds, budget cap,
  coverage) and all large_world suites, dense forest and valley day/night on
  Vulkan/D3D12.
* **Measured**: grass +0.21-0.45 ms GPU for 72-93k blades (D015); cross-fade
  costs nothing measurable against the hard cut (D016).

## 2026-11-06 (CLOD paging, voxel far field, distance thinning, monitors)

* **Implemented**: opt-in in-memory CLOD paging (one page per cluster group in a
  budgeted GPU index pool, pinned roots, async feedback, LRU eviction);
  `VoxelProxyBuilder` far-field splat proxies, used for valley patches beyond
  180 m; `MultiMesh.distance_thinning_*` applied in the GPU culling kernel;
  Performance monitors for GPU-culled instances, CLOD triangles, HZB-occluded
  triangles and CLOD streaming memory.
* **Validated**: new `streaming` and `voxel_proxy` fixtures and a thinning phase
  in `multimesh_lod`; all large_world suites, dense forest and valley day/night
  on Vulkan/D3D12; standard smoke.
* **Measured**: far field saves 0.23-1.0 ms GPU (7-16%) per valley view,
  repeated on Vulkan and D3D12 (D013). Thinning draws 13-23% fewer instances
  with no GPU change in the valley (D014). Streaming has no timing yet (D012).
* **Mistakes recorded**: splats wound as back faces lost direct light under
  `cull_disabled`; a pressure budget taken as a fraction of the cooked size never
  constrained the test views.

## 2026-11-06 (shadow-pass culling, jittered views, kernel binding)

* **Implemented**: opt-in `multimesh_gpu_culling/shadows` culls MultiMesh
  casters per shadow camera (near plane never culls); GPU culling and CLOD now
  run under TAA/upscaler jitter with a jitter-widened frustum (HZB stays off);
  cull kernel binds shared buffers once per mode; shadow counters in statistics.
* **Validated**: all large_world suites on Vulkan/D3D12 (statistics: exact spot
  shadow counts), dense forest with shadow culling and a TAA A/B (0 px), valley
  day/night, standard smoke.
* **Measured**: shadow culling costs 0.05-0.1 ms CPU without a clear GPU gain in
  the valley, so it is off by default (D011). Mistake recorded: positional
  shadows are cached, so static tests must dirty them.

## 2026-11-06 (per-instance MultiMesh LOD on the GPU)

* **Implemented**: the MultiMesh culling kernel picks an index LOD per visible
  instance (up to four levels between the CPU LOD of the bounds' nearest and
  farthest points); reduced-LOD surfaces are now culled too. Statistics report
  visible instances per level. Valley vegetation generates index LODs by default.
* **Validated**: new `multimesh_lod` fixture (exact per-level counts, coverage
  against per-instance CPU LOD, LOD-off pixel identity) and all large_world
  suites on Vulkan/D3D12, dense forest, valley day/night, standard smoke.
* **Measured**: large-range MultiMesh GPU 1.24 ms to 0.42 ms; valley GPU
  unchanged within noise, CPU +0.05-0.1 ms. A fixed-bucket first version
  regressed and was replaced (D010).

## 2026-11-06 (native GPU geometry statistics)

* **Implemented**: `RenderingServer.viewport_get_gpu_geometry_statistics`;
  MultiMesh culling, CLOD selection and HZB kernels add into a per-viewport
  buffer, cleared and read back asynchronously only while polled. Valley HUD and
  bench show the counts.
* **Validated**: new exact-count fixture (`statistics.tscn`) on Vulkan/D3D12,
  all large_world suites, dense-forest A/B, valley day/night and standard smoke.
* **Finding**: CLOD draws 17k–95k triangles in the valley while the frame
  submits 1.3–6.5 M primitives, mostly full-detail MultiMesh vegetation.

## 2026-11-06 (skip GPU culling for fully visible MultiMeshes)

* **Implemented**: MultiMeshes inside all frustum planes keep the direct draw and
  skip their three culling dispatches; never removes geometry.
* **Validated**: large_world suites and dense-forest A/B (new fully visible view:
  identical pixels, no culling dispatch) on Vulkan/D3D12. Valley bench: culling
  CPU overhead 0.1–0.18 ms lower where 112 of 172 patches were fully inside; no
  change in a view without such patches (noise about 0.1 ms).

## 2026-11-06 (CLOD multi-draw submission and valley benchmark)

* **Measured first**: new `devtools/test/forest_bench.ps1` (three fixed valley
  views, four modes, two alternating rounds of 240 frames). Resident CLOD added
  +0.17–0.31 ms render-thread CPU p50 over culling alone because it recorded one
  indirect call per group record.
* **Implemented**: internal `RD::SUPPORTS_MULTI_DRAW_INDIRECT` (Vulkan feature
  bit, D3D12 true, Metal false); CLOD submits each surface's records in one call,
  with the per-record loop as fallback.
* **Validated**: CLOD overhead now +0.02–0.06 ms on Vulkan and D3D12, GPU time
  unchanged; large_world suites, dense-forest pixel A/B, valley day/night and
  standard PhysX/PT/DDGI smoke pass. Also measured: GPU culling saves 0.17–0.36 ms
  GPU but costs up to 0.29 ms CPU; HZB costs 0.03–0.13 ms GPU and saves nothing
  in this scene.

## 2026-11-06 (visual valley smoke tests)

* **Added**: day and night valley fly-through scenes built from ordinary Godot
  nodes (720 m CLOD terrain, about 26k MultiMesh trees in 48 m patches, grass,
  flowers, 280 CLOD rocks, SSR water). Night adds volumetric fog, a lake
  FogVolume, 68 local lights, emissive props and vertex-animated fireflies that
  must use the conventional path. Also kept the dense-forest A/B pixel test.
* **Validated**: day/night on Vulkan and D3D12: all eight route views dispatch
  culling, CLOD and HZB; disabling GPU geometry dispatches none; night light and
  volumetric-fog negative controls differ. Dense-forest A/B passes on both.
* **Testing correction**: the first fly-through read timestamps after
  `get_image()`, which resets them; see the new mistake entry. One-run GPU times
  (enabled vs disabled, one view) differ by about 1%; no performance claim.

## 2026-11-05 (residency cut protocol)

* **Implemented**: CPU coverage snapshots and GPU missing-representation cuts,
  complete coarse fallback and shared requested/used feedback. No page pool or
  streaming claimed; native raster retains full residency.
* **Validated**: production build, Vulkan/D3D12 eight-part suites and standard
  smoke; 24 snapshots, two thresholds, two roots, cut coverage and guarded commands.

## 2026-11-05 (deferred editor help lifetime)

* **Investigated**: repeated editor --import shutdown failure; static help-worker
  callback can outlive EditorNode and enter script-cache work after teardown.
* **Patched**: guard editor lifetime at script-doc-cache entry; no automatic retry
  or broad singleton behavior change. Production build, six repeated imports
  (exit 0, no errors) and standard PhysX/PT/DDGI smoke pass. Other shutdown races
  are not claimed excluded without a crash stack.

## 2026-11-05 (current-frame CLOD HZB)

* **Implemented**: optional complete-depth-then-opaque occlusion, per-viewport
  padded reverse-Z minimum-depth pyramid, guarded native commands and conservative
  clip/coverage/float-uncertainty bounds. No temporal visibility or CPU readback.
* **Validated**: production build, final Vulkan/D3D12 seven-part matrices,
  exact mip oracle, actual command rejection and one-pixel-hole recovery; native
  material/shadow pixels, moving occluders, MSAA/depth fallback and lifetime.
  Numerical uncertainty and 100000-unit source/camera cancellation also pass;
  final standard PhysX/PT/DDGI smoke rerun passes. No performance improvement is claimed.
* **Testing correction**: shared mip-view readback used the wrong base subresource;
  owner-chain readback proves the actual GPU pyramid without a production fix.
* **Unresolved observation**: one editor --import shutdown hit null EditorNode;
  isolated retry, five repeated imports and subsequent complete suites pass.
  Root cause unknown, not hidden by automatic retries.

## 2026-11-05 (scene-import cook and optimized shadows)

* **Implemented**: default-off scene import cluster cooking after final source
  processing, before embedded/external save, once per shared mesh; unsupported
  geometry retains conventional data with warning.
* **Extended**: native CLOD camera depth uses source vertices even with separately
  indexed shadow meshes; shadows retain their own resource.
* **Validated**: production build, class-help checks, real enabled/disabled/morph
  glTF imports and Vulkan/D3D12 six-part matrices. Reversed shadow vertex IDs,
  source/fine pixels and coarse-cast-shadow receiver proof pass. Final standard
  PhysX/PT/DDGI smoke also passes.
* **Runner finding**: first 120-second outer budget expired; targeted import
  checks and complete 300-second-budget reruns pass, with no engine fix inferred.

## 2026-11-05 (native resident CLOD raster integration)

* **Implemented**: ArrayMesh attachment uploads checked
  resident group/index buffers through RenderingServer; per-viewport native
  selection produces source-index commands for existing depth/opaque materials.
* **Preserved**: source geometry/material RID, ordinary shadow/RT geometry,
  conventional fallback for unsupported views, deformation and budgets.
* **Limits**: 4096 groups/surface, 64 MiB/mesh resident index/group data;
  65536 groups and 4096 draws per viewport. No streaming or speedup claim.
* **Validated**: production build and Vulkan/D3D12 five-part matrices. Native
  raster fine/coarse, original materials, three views, transforms, source/RD
  mutation, recook, shadow pixels, fallback and destruction pass. Final standard
  PhysX/PT/DDGI smoke rerun also passes after multi-pass fallback hardening.

## 2026-11-05 (GPU hierarchy selection kernel)

* **Implemented**: source-index group commands selected by projected error and
  ancestor veto, equivalent to exclusive root cuts without a bounded local stack.
* **Validated**: real Vulkan/D3D12 compute versus independent root
  oracle, perspective/orthographic and frustum views, guarded five-word commands.
  Changing-input/recovery and repeated reverse submission pass on both backends.
  Production build and standard smoke pass. Not native raster integration.

## 2026-11-05 (ArrayMesh cluster attachment)

* **Implemented**: serialized optional cluster hierarchy, convenience generation,
  SHA-256 source-array matching, shared-resource change checks and conservative
  mutation invalidation. Materials, names and source RID remain authoritative.
* **Refactored**: same cache validator shared below scene/rendering boundary.
* **Validated**: production build, Vulkan/D3D12 tests of source mismatch, shared
  cache mutation, save/load, region updates and surface lifetime; focused XML
  checks and standard PhysX/PT/DDGI smoke pass. No GPU CLOD submission claimed.

## 2026-11-05 (stable GPU compaction)

* **Fixed**: unordered atomic append replaced by local ranks, block-prefix scan
  and stable scatter, preserving coplanar instance draw order and source identity.
* **Validated**: Vulkan/D3D12 ordered-ID kernel checks across 261 scan blocks,
  shared-view/resource A/B and 100k instances with coplanar colored IDs; standard
  PhysX/PT/DDGI smoke suite passes. No performance improvement claimed.

## 2026-11-05 (native cluster cooker)

* **Implemented**: MeshClusterData resource, module-backed meshlet/group cooker,
  checked cache, border-locked attribute-aware simplification and root-traversed
  reference cuts. Existing meshoptimizer 1.2, no new dependency or copied engine code.
* **Validated**: production build, headless and Vulkan/D3D12 fixture runs;
  curved-grid topology/cut coverage, source preservation, resource roundtrip,
  corrupt-data rejection and extreme-finite projection inputs.
* **Fixed during smoke**: self-closing tutorials section was accepted by RST but
  rejected by native editor help parser; explicit section closing added. Final
  standard PhysX/PT/DDGI smoke rerun passes.

## 2026-11-05 (signed instance-ID compatibility)

* **Fixed**: visible-ID remap must return shader-language `int`, not `uint`.
  Regression shader exercises signed arithmetic and an `int` function argument
  with both conventional and GPU-culled rendering. Production build and
  Vulkan/D3D12 focused regression matrices pass.

## 2026-11-05 (continuous implementation and commits)

* **Updated**: working rules: commit each tested part with its records, then
  continue the explicitly authorized implementation rather than stop per slice.

## 2026-11-05 (native GPU frustum culling)

* **Implemented**: experimental Forward+ MultiMesh compute visibility, stable
  source IDs, per-viewport buffers/indirect commands and conventional fallback.
* **Validated**: final production build; Vulkan/D3D12 native-kernel and native
  raster A/B checks at 65/100k potential instances, dual viewport dispatch,
  resize and shared-resource replacement. Standard PhysX/PT/DDGI smoke rerun passes.
* **Recorded**: explicit virtual geometry / CLOD / far-field voxel follow-on
  scope in `knowledge/VIRTUAL_GEOMETRY.md`; no placeholders called implemented.
* **Updated**: project concepts, source audit, architecture, status and validation;
  new-TU and typed-GDScript-fixture mistakes. Full docs check has unrelated
  existing PhysX errors; focused ProjectSettings docs check passes.

## 2026-11-05 (native large-world rendering foundation)

* **Added**: source audit, architecture, decisions, port status and validation
  runbooks in `knowledge/`; project/native-indirect-foundation.md links them.
* **Found**: indirect MultiMesh setter uploaded the CPU `-1` sentinel as an
  unsigned GPU count; real-RD reproducer checks both surface command layouts.
* **Validated**: production editor build, focused Vulkan/D3D12 command/pixel
  tests and standard PhysX/PT/DDGI smoke suite pass. No scalability/performance claim.
* **Added**: mistakes/python-build-interpreter.md and
  successes/inspect-indirect-before-draw.md.

## 2026-11-02 (release 0.8.1)

* **Updated**: release 0.8.1 (reflections, DDGI adaptive update budget)
  in CUSTOM_BUILD.md; README.md lists the reflection features.

## 2026-11-02 (mirror probes off by default)

* **Updated**: REFLECTIONS.md: `auto_mirror_probes` defaults to off.

## 2026-11-02 (planar distance limits, presets)

* **Updated**: REFLECTIONS.md settings (`planar_max_distance`,
  `planar_mirror_max_distance`), findings, performance plan P7.

## 2026-11-02 (mirrors first for the mirror pass)

* **Updated**: REFLECTIONS.md settings and findings: upright reflectors get
  the mirror pass before floors.

## 2026-11-02 (mirror pass shadow LODs)

* **Updated**: REFLECTIONS.md performance plan P4: shadow passes in the
  mirror pass use the view's whole projection for LODs; mirror occlusion
  culling deferred.

## 2026-11-02 (probe recaptures with a moving sun)

* **Updated**: REFLECTIONS.md performance plan P6: measured, bounded, no
  change needed.

## 2026-11-02 (sky flashing in the mirror)

* **Updated**: REFLECTIONS.md issue J and findings (fog volume created
  before the sky is set up; no reprojection on a new volume's first frame).
* **Added**: mistakes/metric-blind-to-the-artifact.md.

## 2026-11-02 (mirror pass LODs)

* **Updated**: REFLECTIONS.md performance plan: the mirror pass picks mesh
  LODs from the view's whole projection and its own resolution (P4).
* **Added**: mistakes/test-scene-without-hard-quit.md.

## 2026-11-01 (brightness against the path tracer)

* **Added**: mistakes/tonemapped-noise-averages-darker.md.
* **Updated**: REFLECTIONS.md issue I and findings; probe emissive-light limitation.

## 2026-11-01 (DDGI in probe captures, performance plan)

* **Updated**: REFLECTIONS.md: `probe_capture_ddgi`, step 9 done, issue H
  done, performance plan, findings; reflections/planar-open-issues.md.

## 2026-11-01 (reflections: bugs seen in play)

* **Added**: mistakes/motion-test-that-did-not-move.md.
* **Updated**: REFLECTIONS.md findings (mirror pass selection by screen
  coverage, edge halo, mirror pass jitter) and open issues H and I;
  reflections/planar-open-issues.md.

## 2026-11-01 (reflections: open issues)

* **Added**: mistakes/stale-generated-shader-header.md.
* **Updated**: REFLECTIONS.md open issues (A, B, F done; C physical; D
  and G status), findings; reflections/planar-open-issues.md.

## 2026-11-01 (glass research)

* **Added**: research/cheap-glass-shading.md.

## 2026-11-01 (mirror pass cost)

* **Updated**: REFLECTIONS.md findings and limitations: mirror pass cropped
  to the reflector, no post-processing; timestamps before and after.

## 2026-11-01 (mirror pass, step 8)

* **Updated**: REFLECTIONS.md: `planar_mirror_render`, `planar_mirror_scale`,
  findings (clip distances, light culler winding, DDGI sharing, hybrid
  resolve, cost); reflections/planar-open-issues.md.

## 2026-11-01 (reflection resolution, mirrored-camera research)

* **Added**: research/mirrored-camera-reflections.md (step 8 sources).
* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflection_resolution`.

## 2026-11-01 (mirror probes)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_mirror_probes` (a probe in
  front of each mirror, for what close mirrors reflect off screen),
  findings log; reflections/planar-open-issues.md.

## 2026-11-01 (planar quality, paused)

* **Added**: reflections/planar-open-issues.md; REFLECTIONS.md "Open issues"
  (A to G) with repro positions and movement test commands.
* **Updated**: REFLECTIONS.md findings: temporal filter, footprint fill,
  coverage, normal check, automatic scan bounds and filters.

## 2026-11-01 (automatic reflections)

* **Updated**: REFLECTIONS.md: DDGIVolume `auto_reflections` (volume probe and
  planar reflectors found from mesh planes and material roughness).

## 2026-10-31 (reflections)

* **Added**: REFLECTIONS.md (runbook) and reflections/: overview and the
  architecture decision (pixel-projected planar reflections, budgeted and
  DDGI-normalized reflection probes, atlas on demand).
* **Added**: research/pixel-projected-reflections.md,
  research/cubemap-normalization.md.
* **Added**: mistakes/powershell-single-arg-splat.md,
  successes/reflection-atlas-on-demand.md.
* **Updated**: ddgi/improvements/specular-occlusion.md: probe reflections
  are now normalized by DDGI.

## 2026-10-31 (sync)

* **Updated**: CUSTOM_BUILD.md sync log: upstream `95c0fa5fe8` (4.8-beta),
  physx module `97bd583c28`. Shader `CONTAINER_VERSION` 5 -> 6 (upstream
  added a container header). Smoke: PhysX GPU, path tracer and DDGI pass;
  the PhysX tests report one error only because the checkout's `bin/` has
  no Streamline DLLs.

## 2026-10-31 (near-field rays removed, glass rays)

* **Added**: ddgi/findings/near-field-camera-spots.md: near-field rays
  removed from the engine (dark spots moving with the camera).
* **Added**: ddgi/findings/glass-probe-rays.md: four bugs on DDGI rays at
  refractive surfaces, fixed.
* **Updated**: near-field proposal and success marked deprecated.

## 2026-10-12 (performance and stability pass)

* **Updated**: DDGI.md findings: baseline per pass, apply sub-pass
  timestamps, reflection buffer written once, upsample block loads, the
  dropped distance blend skip, the `--chaos` stability test.
* **Updated**: tooling/vulkan-sync-validation.md: a too old layer reports
  false SPIR-V errors.

## 2026-10-12 (dark places)

* **Updated**: DDGI.md step 30: no flicker or blotches in dark places;
  new `tunnel` test view.

## 2026-10-12 (moving objects of any shape)

* **Updated**: DDGI.md step 29: moving objects as oriented boxes with exact
  face form factors (0.7.1).

## 2026-10-12 (moving objects vs path tracer)

* **Updated**: DDGI.md step 28: moving objects' light matched against the
  path tracer.

## 2026-10-12 (moving objects, no noise)

* **Updated**: DDGI.md step 27: moving objects lit as spheres by one ray
  per block, no noise.

## 2026-10-12 (moving objects)

* **Updated**: DDGI.md step 26: probes skip moving objects, near-field
  history lets go faster near them (the smears behind a moving ball).

## 2026-10-12 (near-field rays)

* **Added**: [near-field rays](/ddgi/improvements/near-field-rays.md)
  (proposal 12, done), finding [probes put nearby bounce light in the
  wrong place](/ddgi/findings/near-field-blob.md), success
  [near-field rays](/successes/near-field-rays.md), mistakes
  [a test setting in the animation callback](/mistakes/test-setting-in-animation-callback.md)
  and [a long run of an unfit scene](/mistakes/long-run-of-an-unfit-scene.md).
* **Updated**: [test protocol](/tooling/ddgi-test-protocol.md) with the
  bleed view tests (still and animated).
* **Verified**: emissive surfaces as direct lights (previous entry): the
  storm level shows a smooth light pool from the moving sphere.

## 2026-10-12 (emissive lights, work in progress)

* **Added**: references [DDGI papers and reference code](/research/majercik-ddgi-reference.md)
  and [O3DE DiffuseProbeGrid](/research/o3de-diffuse-probe-grid.md).
* **In progress**: emissive surfaces as direct lights in the DDGI apply pass
  (`rendering/global_illumination/ddgi/emissive_lights`), against light
  spots, blocky glow and bleeding from small emitters. Not verified yet.

## 2026-10-12 (least-squares bake)

* **Implemented**: [least-squares baking](/ddgi/improvements/least-squares-bake.md)
  on `dev-ddgi-quality`; [success](/successes/least-squares-bake.md).
* **Reread** the six GI papers for these two features: SDFDDGI's min/max
  checkerboard went into the low resolution apply; Enemies' dilation
  caveat (it creates new leaks) kept probes inside geometry out of the
  solve and its smoothness term.

## 2026-10-12 (low resolution apply)

* **Implemented**: [low-resolution apply](/ddgi/improvements/low-res-apply-directional.md)
  on `dev-ddgi-quality`, default 1/4: DDGI 1.40 -> 1.13 ms at 1080p.
* **Added**: finding [full resolution pass floor](/ddgi/findings/full-res-pass-floor.md),
  mistake [encoding convention mismatch](/mistakes/encoding-convention-mismatch.md).

## 2026-10-12 (second research set)

* **Added**: 6 references (Unity 6 APV, tetrahedral interpolation, CoD:
  Infinite Warfare precomputed lighting, Stupid SH Tricks, GI downscaling,
  FF7 Rebirth GI) and an unread stub for a Zhihu article (HTTP 403).
* **Added**: proposals 9-11 (low-resolution apply with directional output,
  specular occlusion, least-squares baking) and two small ideas.

## 2026-10-12 (scheduler 2x, moving lights)

* **Corrected**: [favored probes and flicker](/ddgi/findings/scheduler-favored-set-stability.md):
  the cause was the 3x rate, not the window; shipped 2x.
  [Mistake recorded](/mistakes/three-runs-of-a-noisy-test.md).
* **Added**: [moving light trailing](/ddgi/findings/moving-light-trailing.md)
  (open issue, two rejected fixes), the emissive step and weather stepping
  tests in [the test protocol](/tooling/ddgi-test-protocol.md).
* **Updated**: sample-driven results in the proposal and the success page.

## 2026-10-12 (mistakes and successes)

* **Added**: [mistakes](/mistakes/) (7) and [successes](/successes/) (6)
  sections, the `Mistake` and `Success` types, and rule 8 in
  [the howto](/kb-howto.md): record both as they happen.

## 2026-10-12 (later)

* **Added**: [DDGI test protocol](/tooling/ddgi-test-protocol.md), from
  the measurements for sample-driven updates: step response with
  `--sun_step`, startup not reproducible, flicker spread over 3 runs,
  A/B with one editor copy per variant, and a storm game-level test with a
  moving emissive object (fixed exposure, `--fixed-fps 60`, averages
  only).
* **Added**: findings [extra code in the apply pass costs](/ddgi/findings/shader-code-cost-without-work.md)
  and [favored set stability](/ddgi/findings/scheduler-favored-set-stability.md).
* **Updated**: [sample-driven updates](/ddgi/improvements/sample-driven-updates.md)
  is implemented on `dev-ddgi-sched`, with results.

## 2026-10-12

* **Initialization**: Created the bundle: project overview, DDGI and XeGTAO
  state, the findings from the XeGTAO work (render graph ordering issue,
  convergence measuring), six GI papers and talks with what they mean for
  DDGI, and a ranked list of DDGI improvement proposals.
