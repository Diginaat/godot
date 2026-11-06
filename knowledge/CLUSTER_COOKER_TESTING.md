# Cluster hierarchy cooker validation

2026-11-05. Native MeshClusterData resource / existing meshoptimizer 1.2.

```powershell
py -3.14 -m SCons -k -j10 platform=windows target=editor production=yes
bin/godot.windows.editor.x86_64.console.exe --headless --path misc/large_world_test_project --quit-after 300 res://clusters.tscn
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/large_world.ps1 -Driver vulkan
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/large_world.ps1 -Driver d3d12
powershell -NoProfile -ExecutionPolicy Bypass -File devtools/test/smoke.ps1
```

Production build, headless cooker test, both GPU-backend runners and standard
PhysX GPU game/editor, Vulkan PT screenshot and DDGI interior smoke checks pass.
GPU backend runs do not make the CPU cooker a GPU implementation.

Fixture: curved 48×48 grid, normals and UVs, 13,824 source indices. Output:
82 groups, 127 clusters, 3,456 indices in the coarsest root cut. Checks:

- Original mesh vertex/index/attribute arrays unchanged.
- Leaf triangle count and source boundary topology preserved.
- Five thresholds select every leaf region exactly once, without parent/child
  overlap, partial selected groups, internal cracks or changed external edges.
- Coarser cuts do not increase index count and actually simplify this fixture.
- Extreme finite camera/projection inputs do not remove the entire hierarchy.
- ResourceSaver/Loader roundtrip preserves typed hierarchy data.
- Invalid cyclic parent links and out-of-range source vertex indices are rejected
  without destroying valid existing data.
- Source files pass clang-format; focused MeshClusterData class-reference check
  passes. Native editor help parsing passes after replacing self-closing tutorials.

The selector uses root traversal rather than independent parent/self tests;
serialized float bounds can violate numerical projected-error monotonicity.
Double intermediate arithmetic plus hypot avoids finite-float overflow/NaN cuts.

These are correctness measurements, not a frame-time or quality benchmark.

ArrayMesh attachment follow-up (same production flags and backend runners):
source RID unchanged; material/name edits retain cache; mesh plus embedded cache
save/load succeeds; equal-count but different source geometry fails matching;
changed shared cache detaches both old owners; convenience generation succeeds
and invalid generation preserves the old attachment. Vertex/attribute region
updates, added/removed surfaces and clear detach derived data. Source matching
uses SHA-256 over encoded decoded arrays at attachment/load, not per frame.
Both Vulkan/D3D12 matrices and standard PhysX/PT/DDGI smoke pass. Focused XML
checks pass. Initial build needed an explicit callable_mp include in mesh.cpp.

Subsequent resident-raster integration is validated by `clod_raster.tscn` in the
same Vulkan/D3D12 runners. It asserts native timestamp markers in three views,
source/fine byte-identical pixels using StandardMaterial3D and ShaderMaterial,
reflected/nonuniform transforms, viewport resize, visibly changed coarse raster,
return to fine, mutation/recook, direct RD index invalidation, custom vertex and
material-next-pass fallback and destruction. A separate top-down floor crop
excludes source geometry, contains measurable shadow, and remains byte-identical
at coarse camera CLOD; shadow draw counts remain nonzero/unchanged. Production
build, both expanded five-part runners and final standard smoke rerun pass.

Scene-import follow-up adds real glTF enabled/disabled/morph fixtures, shared-mesh
and saved-signature checks, plus native raster with independently reversed shadow
vertex IDs. Both six-part backend runners pass. Run with a 300-second outer tool
timeout; the expanded suite exceeded a first 120-second budget. External mesh-save
branch is implemented but not independently covered by these embedded fixtures.
Final standard smoke follow-up passes.

No virtual residency, streaming or voxel representation is claimed. No dependency
update or engine source transplant was needed.

## Current-frame HZB follow-up

The expanded seven-part runners include `hzb.tscn` and native CLOD/HZB raster.
Use a 300-second outer tool budget and retain each scene's hard frame cap
(600 for expanded native raster, 300 for the other fixtures).

GPU proof reads the owning mip chain, not shared-view readback offsets. All
13×9 source / 16×16 padded levels match CPU reverse-Z min reduction. Guarded
commands prove real hidden/front/touch/clip/edge cases, zero-count preservation,
one-pixel-hole hide/recover/hide and floating-uncertainty retention. Native
raster adds moving walls, HZB markers in three views, unchanged source/fine and
cast-shadow pixels, MSAA and custom-depth fallback, source/index mutation,
destruction and 100000-unit world/camera cancellation. Both final Vulkan/D3D12
matrices, production build and final standard smoke pass. No speedup
or reduction of depth/shadow work is claimed.

Observed once: editor --import shutdown with null EditorNode/0xC0000005.
Isolated retry, five repeated imports and subsequent complete backend runners
passed without a production workaround. Root cause remains unresolved.
