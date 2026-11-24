# Optimize Diginaat/Godot dev — implementation prompt

Work directly in the source of https://github.com/Diginaat/godot/tree/dev. Deliver a faster Godot editor and matching release export template that run existing games. Optimize CPU frame time, GPU frame time and frame pacing at unchanged visual quality, from a furnished room to a city with traffic, AI, neon, shadows and streamed interiors. Use ordinary Godot nodes and preserve script APIs, scene files, gameplay timing and renderer fallbacks. Favor readable changes that the fork can maintain across upstream updates.

## Execution order

Complete steps 1 and 2, then implement and validate the built-in level streamer in step 3 even if another subsystem dominates the profile. After its exported-game acceptance test, use the measurements to select one optimization lane in step 4. Remeasure after each accepted patch and use step 5 for delivery.

### 1. Pin the source and establish a runnable control

- Inspect git status, fetch dev, record the SHA and create a clean local worktree/branch. Protect local changes. The research checkpoint was 1e85ad25666b5e2459dd58b794673a0b0966acd0. Read AGENTS.md, CUSTOM_BUILD.md, AGENT_MEMORY.md, the required OKF KB instructions, renderer MEMORY/roadmap/benchmarks, and the scene-runtime memory. For affected lanes read DDGI.md and PATHTRACER_TESTING.md. Check current code and past failed experiments before changing anything.
- Build the host editor and matching release template using the fork's recipes and configured modules, including .NET where configured. Export and launch the baseline fixture. Keep the binary, build flags, driver, project settings, fixture revision and command lines. Use release game runs for final timings.

### 2. Find which part of the frame is late

Reuse existing fixtures. Add only the missing workload or counters required to distinguish the first candidate.

| Control | Run | Question |
|---|---|---|
| Small scene | Empty viewport, furnished room, then 1/16/64/256 visible meshes. Compare equivalent MeshInstance3D and direct RenderingServer RID scenes. | Is the fixed cost in nodes, server submission, or GPU passes? |
| Busy city | Fixed route through a neon intersection, moving cars, crowds, an interior and a long sightline. Toggle one subsystem at a time, then run everything together. | Does the deadline depend on CPU simulation, submission, shadows, shading, geometry or GI? |
| Streaming boundary | Drive past detailed shop signs, turn into a shop, switch a light, make a U-turn and teleport. Compare with the same route fully preloaded. | Is the hitch in I/O, upload, scene activation, pipeline compilation or first use? |

Start at native 1080p on RTX 3060 if available, fixed camera path, seed, exposure and simulation inputs. Disable frame generation, VSync and frame caps for throughput measurements. Record native raster and raster + DDGI separately; profile path tracing only on its own route. Warm imports and shaders for steady-state tests; record cold-start and cold-streaming runs separately. Establish A/A noise with repeated baseline runs. Capture main and render CPU, full GPU frame, expensive passes, p50/p95/p99 wall-frame time, worst boundary frame and peak RAM/VRAM. CPU and GPU timings overlap. Inspect the timeline before assigning the bottleneck. Run final timings without heavy tracing.

### 3. Build and ship the built-in level streamer (required)

Implement a reusable, opt-in 3D level streamer in the fork's C++ source. Expose it to both the editor and exported games. Build on Godot's existing resource and scene APIs. First inspect the fork for reusable pieces. The feature must be usable by a second project through ordinary Godot authoring; a demo-only manager does not complete this milestone.

- **Authoring.** Provide a streamer node and an Inspector-editable cell definition or equivalent native workflow. Let creators select ordinary .tscn files, place their cells, identify neighbors, select the streaming target, and configure preload and unload distances. Give each cell a stable ID. Validate duplicate IDs, missing targets/resources and invalid distances. Make cell scene references export-discoverable while keeping their contents lazily loaded. Projects that do not use the feature should incur no per-frame scanning.
- **Runtime.** Use threaded ResourceLoader requests, poll status and collect completed resources. Stage active-tree changes on the owning thread; distinguish loaded, activated and safe to enter. Allow different preload/unload distances to avoid churn at boundaries. Bound outstanding requests and activation work. Make repeated requests idempotent and handle shared dependencies, U-turns, teleports, load failures and stale completions. Retire old cells only when safe. Preserve callback order within authored subscenes and release resources after unload.
- **Visual and gameplay readiness.** Define how a game knows a destination is ready. Prefetch visible textures and dependencies for lighting across cell boundaries. Keep reachable collision and required navigation valid. Measure first-visible-frame quality against the fully loaded route and report cold-teleport wait separately. Provide lifecycle hooks keyed by stable cell ID so game code can save and restore state. Demonstrate a door remaining open or closed after its cell actually unloads and reloads.
- **Acceptance.** Build the editor and matching template. In a clean project, configure the streamer via the Inspector, export the project, and launch it without access to the source project. Drive down a street, enter a shop, immediately turn back, then travel far enough to force unload and revisit it. Verify textures, lighting, collision, navigation and door state. Check a cold teleport, rapidly crossing the boundary and a destination load failure. Record p99, worst boundary frame, readiness delay and peak RAM/VRAM against step 2. Document the minimal setup and report exact commands and artifact paths. If more than one session is required, checkpoint this milestone as incomplete and resume it before moving on.

### 4. Optimize the highest measured cost

**A. CPU-bound nodes, moving objects or RenderingServer**

1. Trace moving-node work through transform propagation, callbacks, physics sync, dirty instance packing, queue waits and submission. Sweep idle, sparse and dense motion; compare native, GDScript and C# where available. Inspect existing process lists, prefetch, epoch propagation and dirty-upload batching. Preserve callback order, pause/priority semantics, immediate transform reads, reparenting, deletion and interpolation.
2. Remove measured redundant work first. If pointer chasing dominates, test compact ordered hot fields for a single process group. If packing dominates, compare a contiguous serial pass against coarse disjoint worker ranges. Include maintenance, allocations, memory per node and merge time. A generalized ECS conversion requires stronger evidence than a single loop improvement.
3. For server stalls, distinguish producer wait, queue residence, execution and presentation. Coalesce only operations with no observable intermediate result. Respect reads, callbacks, RID free/reuse and draw boundaries. Use existing async buffer/texture readback for consumers that explicitly tolerate delayed results. Verify frame/view identity and resource lifetime.
4. Enable proven internal parallel work automatically using the existing WorkerThreadPool. Compare serial, dispatch-and-immediate-wait, and dispatch-with-useful-overlap. Keep tiny batches inline, join by the actual dependency, and test threadless/single-worker execution. For background node jobs, snapshot inputs, bound queued bytes, reap tasks and validate owner/generation on delivery. Keep scripts in their configured thread groups and apply scene mutations on the owning thread. Verify the default with an untouched exported project.

**B. GPU-bound native raster**

1. If lights dominate, profile cluster building, shading and shadow redraws separately. Sweep 128/512/2,048 placed lights, then vary overlap at a fixed visible count. Record accepted lights and cap overflow. Trace one car under stationary lamps, then moving headlights. Tighten conservative shadow invalidation only if unnecessary redraws are demonstrated. Test old/new caster bounds, off-camera shadows, skinned actors and despawns.
2. If depth or geometry dominates, inspect the fork's existing GPU instances, CLOD, MultiMesh and HZB paths. Reconcile the current G3 status before an experiment. Compare the same mesh count in an open and an occluded room. Measure pyramid build, depth passes, indirect draws and total frame. Conservative skip rules must preserve all consumers, camera cuts and multi-viewport history. For distant city blocks, benchmark existing HLOD and CLOD; publish any representation change as an optional quality tier. Separate triangle cost from glass, rain and foliage overdraw.
3. If hitches correlate with cold pipeline creation, inspect missing precompilation or redundant keys. Retest cold with the same cache conditions. Preserve shader behavior, transparency ordering and motion vectors.

**C. GPU-bound DDGI and emissives**

1. Fix ray count, probe spacing, update schedule, sampling, exposure and resolution. Use the existing room, tunnel and outdoor fixtures. Capture trace, blend, apply, upsample and resolve separately. First try exact redundant upload/store elimination or producer/consumer fusion where precision, history, barriers and sampling footprints allow it. The fork already rejected skipping distance blending after the tunnel became brighter and flickered.
2. If acceleration structures dominate with moving actors, separate transform-only, deformation and topology changes. Compare update/refit against rebuild including subsequent ray traversal. Check light changes, camera scroll, doorway leaks and disocclusion.
3. Isolate sign emission, bloom, indirect bounce and actual local lights. Use baked DDGI or LightmapGI as comparison modes for static content. Optional better lighting or advanced emissive sampling belongs in a separate matched-GPU-budget experiment. Preserve dynamic lighting response for the unchanged-quality lane.

**D. Streaming and level-transition hitches**

1. Attribute texture feedback, mip selection, reads, reloads, upload, binding and first visible use in the existing texture_streaming module. If texture residency is late, deduplicate superseded work or use upcoming cell dependencies to prefetch. Budget in-flight and resident bytes, reserve room for visible mips and retain reference compression, anisotropy and sampling. Test signage, normal maps, grazing angles, alpha cutouts, reflections and a rapid turn. Count frames missing required mips.
2. If activation is late, use ResourceLoader's threaded request/status/get sequence and collect when ready. Measure instantiation, enter-tree/ready, upload, physics/navigation registration and first draw independently. Profile the built-in streamer from step 3 on its street/interior fixture. Prepare dependencies early, preserve callback order, publish on owning threads, and make collision/nav and neighboring lighting ready by the first reachable frame. Preserve persistent NPC/door state, cancel obsolete requests and use unload hysteresis. For cold teleports report readiness delay, including any loading screen.
3. Combine texture and level prediction only after validating each source of hitch independently. Compare first-visible-frame images to the fully resident reference and check repeated boundary crossings for leaks or eviction churn.

**E. Crowd, AI, animation or physics CPU cost**

1. Isolate perception, AI decisions, reachable/unreachable path searches, avoidance, animation evaluation, skinning, contacts and awake bodies. Test an intersection crossing, a blocked door, parked traffic and a collision pile-up. Improve exact duplicated work and invalidation first.
2. Where game design allows different response rates, expose an opt-in significance policy for background agents. Stagger requests with bounded deadlines; share path or pose data only for compatible states; invalidate on topology, target or animation changes. Track reaction age, stuck agents, missed animation events, root motion, and nearby visual fidelity. Keep authoritative gameplay and collisions correct off-screen. Evaluate these policies separately from transparent engine improvements.

### 5. Accept, integrate and ship

- Alternate baseline/candidate runs at least three times each on equivalent inputs; collect longer traces for p99 or small differences. Report absolute milliseconds and variance. Require a gain beyond noise in the target case, test the small-room and combined city controls, and explain memory and latency tradeoffs. If only a subsystem improves, say so.
- For unchanged-quality patches, compare deterministic buffers exactly where feasible and otherwise check against A/A variation using fixed-seed HDR/GI buffers, crops and motion sequences. Check first frames after a cut, switching a sign, streamed arrival and viewport resizing. Keep dynamic resolution, lower mip caps, reduced ray counts and simulation throttling out of unchanged-quality results.
- Run the relevant existing tests and smoke suites. Validate the final editor and an exported game with matching release template. Check D3D12 raster on Windows and older non-RT hardware where available. Repeat the finalist at 3440×1440 if practical. Mark unavailable hardware explicitly and give exact reproduction commands; deliver verified CPU/build work where possible.
- Commit each accepted change with benchmark evidence. Update existing renderer/runtime memory and OKF logs according to the repo. Provide baseline/final SHAs, binary paths, build/run commands, before/after table, image/video evidence, rejected experiments and the next highest-cost measured stage. Rerun the combined workload after each accepted change because the bottleneck can move.

## Starting references

- [Fork renderer roadmap](https://github.com/Diginaat/godot/blob/dev/docs/renderer/roadmap.md), [benchmarks](https://github.com/Diginaat/godot/blob/dev/docs/renderer/benchmarks.md), [scene runtime](https://github.com/Diginaat/godot/blob/dev/docs/scene-runtime/MEMORY.md), [DDGI](https://github.com/Diginaat/godot/blob/dev/DDGI.md)
- [Godot worker pool](https://docs.godotengine.org/en/stable/classes/class_workerthreadpool.html), [thread-safe APIs](https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html), [RenderingDevice](https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html)
- [Texture streaming](https://docs.godotengine.org/en/latest/tutorials/rendering/texture_streaming.html), [background loading](https://docs.godotengine.org/en/stable/tutorials/io/background_loading.html), [navigation performance](https://docs.godotengine.org/en/stable/tutorials/navigation/navigation_optimizing_performance.html), [pipeline compilations](https://docs.godotengine.org/en/stable/tutorials/performance/pipeline_compilations.html)
- [RTXGI algorithms](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Algorithms.md), [Khronos indirect draws](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html), [GPU Trace](https://developer.nvidia.com/nsight-graphics-features)