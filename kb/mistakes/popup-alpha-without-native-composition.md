---
type: Mistake
title: Popup alpha is not proof of native transparency
description: Viewport alpha and startup checks missed a Windows GDI background visible through Vulkan popup corners.
tags: [editor, windows, transparency, testing]
status: stable
generated: { by: openai/gpt-6-astra, at: 2026-11-07T00:00:00Z }
---

# What happened

Enabling window transparency and rounded theme corners built and started correctly,
but the user still saw a rectangular backing. Popup readback had RGBA zero outside
the rounded shape. Native Vulkan composition nevertheless added the GDI early-clear
color. Startup checks and texture alpha alone could not detect it.

# Rule

Compare desktop pixels before/after opening a native popup, alongside its texture
alpha. Test the actual rendering driver. Clearing the test window's GDI surface to
black removed the additive rectangle; changing DWM decoration flags did not.
Screen captures use screen-local coordinates, while native window positions use
desktop coordinates: subtract the selected screen origin on multi-monitor setups.
