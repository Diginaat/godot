---
type: Technique
title: Vulkan synchronization validation without the SDK
description: Use an existing VkLayer_khronos_validation DLL through a scratch manifest and VK_ADD_LAYER_PATH, with sync validation on.
tags: [vulkan, validation, synchronization, debugging]
status: stable
generated: { by: claude_code/claude-opus-5-5, at: 2026-10-12T00:00:00Z }
---

# Problem

`--gpu-validation` does nothing visible when the Khronos validation layer
isn't installed: the log only shows the loader's implicit layers. Check for
`VK_LAYER_KHRONOS_validation` in the log before reporting a clean run.

# Technique

1. Find a `VkLayer_khronos_validation.dll` already on the machine (other
   applications ship one; or install the Vulkan SDK).
2. Write a manifest JSON for it in a scratch folder:

```json
{"file_format_version":"1.2.0","layer":{"name":"VK_LAYER_KHRONOS_validation","type":"GLOBAL","library_path":"<absolute path to the DLL>","api_version":"1.4.300","implementation_version":"1","description":"Khronos validation"}}
```

3. Run with `VK_ADD_LAYER_PATH=<scratch folder>`,
   `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` and `--gpu-validation`.
4. Filter the log for `SYNC-HAZARD` and `VUID`.

# Notes from use (2026-10-11)

- Reported staging-copy write-after-write hazards between command buffers
  and `VUID-vkCmdTraceRaysKHR-None-08608` (viewport dynamic state set before
  the trace): both outside DDGI.
- It found nothing for the [DDGI ordering issue](/ddgi/findings/render-graph-bounce-loss.md).
