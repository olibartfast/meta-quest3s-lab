# Milestone 2 — Vulkan Stereo Rendering

## Goal

Render repository-owned graphics correctly in both eyes using OpenXR stereo swapchains and Vulkan. Replace the Milestone-1 empty-frame submission with a real stereo frame loop: locate views, acquire swapchain images, render a colour triangle per eye, and submit projection layers.

## Architecture

```
libs/vulkan_renderer/                    apps/02-vulkan-stereo-triangle/
├── include/vulkan_renderer/             ├── CMakeLists.txt
│   ├── vulkan_swapchain.h               ├── main.cpp
│   ├── vulkan_pipeline.h                ├── README.md
│   ├── vulkan_render_pass.h             ├── src/main/
│   ├── vulkan_shaders.h                │   ├── AndroidManifest.xml
│   └── vulkan_error.h                   │   └── cpp/
├── src/                                 │       ├── main.cpp
│   ├── vulkan_swapchain.cpp             │       ├── shaders.vert
│   ├── vulkan_pipeline.cpp              │       └── shaders.frag
│   ├── vulkan_render_pass.cpp           └── build.gradle
│   ├── vulkan_shaders.cpp
│   └── vulkan_error.cpp
└── CMakeLists.txt
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext    (existing, libs/xr_core)
  ├── VulkanSessionBinding (existing, libs/xr_core — extended with command pool)
  ├── XrSessionContext     (existing, libs/xr_core — extended with locate-views)
  ├── VulkanSwapchain      (new, libs/vulkan_renderer)
  │     ├── enumerate view-config views → per-eye width/height
  │     ├── create colour swapchains (1 per eye)
  │     ├── wrap each in a framebuffer (render pass + image view)
  │     └── acquire / release per frame
  ├── VulkanPipeline       (new, libs/vulkan_renderer)
  │     ├── compile SPIR-V shaders
  │     ├── pipeline layout, descriptor set (UBO for view/proj)
  │     └── graphics pipeline (vertex + fragment)
  └── Render loop          (main.cpp frame loop)
        ├── xrWaitFrame → predicted display time
        ├── xrBeginFrame
        ├── xrLocateViews → per-eye view + projection matrices
        ├── for each eye:
        │     ├── acquire swapchain image
        │     ├── update UBO, begin render pass, draw triangle, end render pass
        │     └── release swapchain image
        └── xrEndFrame (with projection layers)
```

## Portability approach

- **OpenXR swapchain lifecycle** — colour swapchains created via `xrCreateSwapchain`, images acquired with `xrAcquireSwapchainImage`/`xrWaitSwapchainImage`, released with `xrReleaseSwapchainImage`. All portable.
- **Vulkan through OpenXR** — graphics requirements, physical device, instance, and device creation all flow through `XR_KHR_vulkan_enable2`. No raw `vkCreateInstance`/`vkCreateDevice`.
- **SPIR-V shaders compiled offline** — shaders committed as SPIR-V binaries (`shaders.vert.spv` / `shaders.frag.spv`). Optionally a host-side `glslc` step documented in README for regeneration. No runtime GLSL compilation.
- **No Meta-specific extensions** — stereo rendering works on any OpenXR runtime with `XR_KHR_vulkan_enable2` and `PRIMARY_STEREO` view configuration.
- **Single UBO for view/projection matrices** — `struct FrameUniforms { mat4 view; mat4 proj; }` per eye. Descriptor set updated each eye via `vkCmdPushConstants` or uniform buffer sub-range. Standard Vulkan, no vendor-specific paths.
- **Validation layers gated by `#if !defined(NDEBUG)`** — enabled in debug builds only, with `VK_LAYER_KHRONOS_validation` requested if available.

## Component breakdown

| File | Responsibility |
|------|---------------|
| `vulkan_error.h/cpp` | `VkCheck()` macro wrapping `vkResult` → string (use `#include <vulkan/vulkan.h>`); `LogInfoVk`/`LogErrorVk` for vulkan_renderer namespace |
| `vulkan_swapchain.h/cpp` | Enumerate view config views → per-eye resolution; create colour swapchains via `xrCreateSwapchain`; create `VkImageView` per swapchain image; create `VkFramebuffer` per image; acquire/image-wait/release cycle; destroy all resources |
| `vulkan_render_pass.h/cpp` | Create a single `VkRenderPass` with one colour attachment (load clear, store store), compatible with the swapchain image format |
| `vulkan_shaders.h/cpp` | Load pre-compiled SPIR-V bytecode into `VkShaderModule` objects; return `VkPipelineShaderStageCreateInfo` for vertex and fragment stages |
| `vulkan_pipeline.h/cpp` | Descriptor set layout (1 UBO at binding 0 for vertex stage); pipeline layout; `VkGraphicsPipelineCreateInfo` with triangle-list topology, no depth/stencil, viewport/scissor set dynamically; destroy pipeline + layout |

### Existing components extended

| File | Extension |
|------|-----------|
| `VulkanSessionBinding` | Add `VkCommandPool` creation (reset flag); expose `Queue()`, `Device()`, `QueueFamilyIndex()` accessors. Add `CreateCommandPool()` method. |
| `XrSessionContext` | Replace `PumpEmptyFrame()` with `BeginFrame()`, `LocateViews()`, `EndFrameWithLayers()`. Store per-eye `XrView` and `XrCompositionLayerProjectionView` arrays. Expose `ViewCount()`, `Views()`, `ProjectionLayers()`. |
| `xr_session.h` | Add `XrViewConfigurationView` caching (enumerated at init). Expose `ViewConfigurationViews()` for swapchain sizing. |
| `main.cpp` | Full frame loop: wait → begin → locate views → per-eye render → end with projection layers. Per-frame UBO update with view/projection from `xrLocateViews`. |

## App flow (`main.cpp`)

```
Android entry → attach JNI → xrInitializeLoaderKHR
→ enumerate extensions → validate required
→ create XrInstance → query XrSystemId
→ get Vulkan graphics requirements → create Vulkan instance, device, queue, command pool
→ create XrSession (with existing Vulkan binding chain)
→ enumerate view configuration views → cache per-eye width/height/sampleCount
→ create swapchains (1 per eye, type color, width×height)
→ create render pass (single color attachment)
→ for each swapchain image, per eye:
    create VkImageView → create VkFramebuffer
→ compile shaders (load SPIR-V) → create pipeline layout → create pipeline
→ create uniform buffer (per-frame, host-visible) → map memory

→ event loop:
    poll Android events → poll XR events
    state transitions: READY→xrBeginSession, STOPPING→xrEndSession, EXITING→exit
    when session running:
        xrWaitFrame → predictedDisplayTime
        xrBeginFrame
        xrLocateViews (LOCAL space, predicted display time) → per-eye view+projection
        for each eye (0, 1):
            xrAcquireSwapchainImage → xrWaitSwapchainImage
            memcpy view+proj into UBO mapped memory
            vkBeginCommandBuffer → vkCmdBeginRenderPass (clear grey)
              → vkCmdBindPipeline → vkCmdSetViewport/vkCmdSetScissor (per-eye)
              → vkCmdBindDescriptorSets → vkCmdDraw (3 vertices)
              → vkCmdEndRenderPass
            → vkEndCommandBuffer → vkQueueSubmit → vkQueueWaitIdle
            xrReleaseSwapchainImage
        xrEndFrame (with projection layer info: per-eye swapchain + fov + pose)

→ cleanup: wait idle → destroy swapchains/framebuffers/image-views → destroy pipeline → destroy render pass → destroy command pool → xrDestroySession → destroy Vulkan device/instance → xrDestroyInstance → detach JNI
```

### Stereo rendering specifics

- Two `XrCompositionLayerProjectionView` entries (index 0 = left, index 1 = right).
- `XrCompositionLayerProjection` wraps the layer views; submitted as `layers[0]` in `XrFrameEndInfo`.
- Each projected layer references the correct eye swapchain via `subImage.swapchain`, `subImage.imageRect`, and `subImage.imageArrayIndex = 0`.
- Viewport covers the full swapchain extent for each eye.
- Triangle vertex data in NDC (`-1..+1`), scaled/coloured per eye so each eye sees a different-colour triangle for visual confirmation of stereo separation.

## Build system

- `libs/vulkan_renderer/CMakeLists.txt` — static library target `vulkan_renderer`, links Vulkan and OpenXR::openxr_loader.
- `apps/02-vulkan-stereo-triangle/CMakeLists.txt` — shared module `vulkan_stereo_triangle`, links `vulkan_renderer`, `xr_core` sources (or optionally `xr_core` as a static library if refactored), android_native_app_glue, android, log, vulkan, OpenXR::openxr_loader.
- `apps/02-vulkan-stereo-triangle/build.gradle` — mirrors `01-openxr-bootstrap` build.gradle. Package `com.olibartfast.questlab.vulkanstereotriangle`. Same ndkVersion, compileSdk, ABI filter.
- `apps/02-vulkan-stereo-triangle/src/main/AndroidManifest.xml` — mirrors `01-openxr-bootstrap` manifest: NativeActivity, immersive HMD intent, vulkan hardware feature declared. Library name `vulkan_stereo_triangle`.
- `settings.gradle` — add `include(":apps:02-vulkan-stereo-triangle")`.
- `scripts/build_deploy.sh` — add `--app 02-vulkan-stereo-triangle` option.

## Shader compilation

- Commit SPIR-V binaries alongside source GLSL files.
- Add a `compile_shaders.sh` script in `apps/02-vulkan-stereo-triangle/` that runs `glslc` (from Vulkan SDK / NDK shader tools) to regenerate `.spv` from `.vert`/`.frag`.
- At build time, CMake includes the `.spv` files as binary resources in the APK's assets, or embeds them as `const uint32_t[]` arrays via `xxd -i` equivalent or CMake `file(READ ... HEX)`.
- Prefer embedding as `const uint32_t[]` in a `.inc` header to avoid runtime file I/O on Android.
- If NDK shader tools are unavailable at build time, fall back to checking in pre-compiled SPIR-V bytecode arrays.

## Definition of Done

A stable stereoscopic triangle is visible in the headset with correct per-eye projection and no persistent OpenXR or Vulkan validation errors. Specifically:

1. The application launches on Quest 3, creates an active OpenXR session, and enters the render loop.
2. Each eye sees a triangle rendered with the correct view and projection transform.
3. The triangle is visually distinct per eye (e.g. different colour channel per eye) so stereo separation can be confirmed on-device.
4. Full OpenXR frame lifecycle executes without errors: `xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → acquire/render/release per eye → `xrEndFrame`.
5. Clean shutdown destroys all swapchains, image views, framebuffers, pipeline, command pool, render pass, Vulkan device, and Vulkan instance without validation-layer complaints.
6. Three consecutive launch/exit cycles complete without crashes or leaked handles.
7. `adb logcat -s VulkanStereoTriangle` shows structured logging of swapchain creation, frame pacing, and shutdown.

## Risks & Open Questions

- **Swapchain format compatibility**: Must query supported swapchain formats via `xrEnumerateSwapchainFormats` and select `VK_FORMAT_R8G8B8A8_SRGB` or fall back to the first available format.
- **Command buffer reset strategy**: Per-frame `vkResetCommandBuffer` + re-record vs. pre-recorded secondary command buffers. Initial approach uses per-frame reset + re-record for simplicity.
- **Framebuffer count vs. swapchain image count**: OpenXR swapchain image count is chosen by the runtime; framebuffers must match exactly. Always query `xrEnumerateSwapchainImages` after creation.
- **Frame timing**: The `predictedDisplayTime` from `xrWaitFrame` must be passed to `xrLocateViews` to get accurate view matrices.
- **Shader compilation**: `glslc` must be available in the NDK toolchain (shipped with NDK r29 under `toolchains/llvm/prebuilt/linux-x86_64/bin/`). Verify before build.
- **Memory barriers**: Swapchain images acquired from OpenXR come with `VK_IMAGE_LAYOUT_UNDEFINED`. Initial render pass layout transition to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` is needed. Final layout before `xrReleaseSwapchainImage` must be `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` (OpenXR requires images in this layout).
- **Synchronisation**: `vkQueueWaitIdle` per eye is simple but wasteful. A fence/semaphore per eye with `vkWaitForFences` is preferred for production but wait-idle is acceptable for Milestone 2.
- **Validation layers**: Quest 3 runtime may include Vulkan validation layers. Debug builds explicitly enable `VK_LAYER_KHRONOS_validation`; if unavailable, log a warning and continue without validation.
