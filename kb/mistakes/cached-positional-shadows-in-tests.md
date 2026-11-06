---
type: Mistake
title: Static test expected a spot shadow pass every frame
description: Positional (spot/omni) shadows are cached in the shadow atlas and re-rendered only when the light or a caster changes, so a static fixture saw no shadow pass after startup.
tags: [testing, shadows, gpu-culling, statistics]
status: stable
generated: { by: claude-code/claude-opus-5-5, at: 2026-11-06T00:00:00+01:00 }
---

# Observed failure

The statistics fixture added a shadowed spot light over a MultiMesh and expected
exact shadow-pass culling counts. The counters stayed at zero on Vulkan, even
though shadow-pass culling worked in the dense-forest test.

# Cause

Godot keeps positional shadows in the shadow atlas and only re-renders a light's
shadow when the light or an instance inside it changes. In a static scene the
spot shadow is drawn once, during the first frames, before the fixture starts
polling. Directional shadows are rendered every frame, which is why the forest
test (directional sun) dispatched shadow culling.

# Rule

When a test needs a positional shadow pass in a given frame, make the shadow
dirty in that frame. The fixture nudges the spot light by 0.1 mm every frame,
which keeps the pass live without changing counts. Use directional lights when
a test only needs some shadow pass every frame.
