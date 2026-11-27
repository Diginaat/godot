---
type: Finding
title: Lights over the cluster limit keep the nearest ones
description: Forward+ dropped visible omni, spot and area lights above max_clustered_elements in cull order, before the distance sort, so a light next to the camera could vanish. Now each type keeps the lights whose range is nearest to the camera (nth_element), with a one-time warning.
tags: [lighting, clustered, forward-plus, correctness]
status: stable
sources:
  - id: test
    title: misc/large_world_test_project/light_cap.gd (large-world suite), 2026-11-27
generated: { by: claude_code/claude-opus-5-5, at: 2026-11-27T00:00:00Z }
---

# What

`LightStorage::update_light_buffers()` collected visible positional lights
into fixed arrays of `max_lights` (`rendering/limits/cluster_builder/max_clustered_elements`,
default 512) and skipped every light past the cap, then sorted the kept
ones by distance. Which lights were dropped depended on the scene cull's
order (instance array order), not on distance: with 520 far lights and two
near lights, the near light listed last was not drawn (the test fails on
the old code).

# Fix

All visible lights of a type are collected (`light_candidates`). Above the
cap, `SortArray::nth_element` keeps the `max_lights` with the smallest
reach (distance to the light minus its range, plus half the diagonal for
area lights), so lights whose range contains the camera always stay. A
warning names the setting once. The kept lights are then sorted by
distance as before. Cost: one linear selection over the visible lights,
only when over the cap.

# Tests

`light_cap.gd`: red near light added first, 520 dim far lights, green near
light added last; both near spots must take their colour. Passes on
Vulkan and D3D12, fails on the old code (red dropped).

# Limits and next

The cap itself stays: the cluster builder's buffers are sized by it
(element bits plus a depth word per element in every tile). Raising it
without that memory, and many-light scaling in general, is the
hierarchical clustered lighting step (roadmap Phase 7 / G7).
