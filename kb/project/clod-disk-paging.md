---
type: Finding
title: CLOD pages from disk
description: A checked page file lets the renderer hold only root indices and read other cluster groups on worker threads.
tags: [geometry, clod, streaming, io]
status: experimental
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T00:00:00Z }
---

# What it does

`MeshClusterData.save_pages(path)` writes every surface's cooked indices to a
`.clodpages` file and records the path, a SHA-256 of the surface's indices,
groups and clusters, and the surface entry in the surface record. With
`rendering/geometry/cluster_lod/streaming/enabled`, a surface whose record
names a matching file keeps only its root groups in renderer memory; the other
groups are read from the file when cluster selection asks for them.

# How it works

* Header check at setup on the render thread: magic, version, surface entry,
  hash, index count and data range. Mismatch or missing file: one warning,
  memory streaming.
* Requests from the existing GPU feedback are grouped into one batch per file
  and frame and read by a `WorkerThreadPool` task (at most 1M indices in
  flight). The render thread collects finished batches at the next frame
  start; nothing is shared while a task runs, so there is no lock.
* Each setup gets a new id. Reads for a released or re-set-up surface are
  dropped. Finished reads are collected even when no streamed surface is left.
* Read indices are checked against the vertex count before upload. A failed
  group is never read again until the next setup; its ancestors draw.

# Evidence

`streaming_disk` fixture, Vulkan and D3D12: 21 pages from disk give the
resident image byte for byte with 77,808 of 1,168,728 cooked bytes held; a pool
rebuild reads them again; out-of-range page data fails 2 reads once and keeps
coverage; a file with another mesh's hash streams from memory with an
identical image; meshes freed with 28 KB of reads in flight release cleanly.

# Limits

`save_pages(path, false)` pages the indices out of the resource too: it keeps
only the roots, and the renderer reads the rest from the file, or the whole
file at setup without streaming; a missing file means no cluster LOD for that
mesh (D020). Scene import writes page files this way with
`meshes/cluster_lod_page_files` (D019; `import_paged` fixture: 72 disk reads,
image equal to resident, 13,824 of 96,768 bytes held). No timing measurement. Only the nearest fixture mesh needs groups
beyond its roots in the test view.
