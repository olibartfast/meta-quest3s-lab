# Milestone 5 — Passthrough Mixed Reality

## Goal

Display a repository-rendered Vulkan object over Quest camera passthrough using `XR_FB_passthrough`. Introduce a portable passthrough interface behind which a Meta-specific implementation creates one reconstruction layer, submits it as a composition underlay, and transparently composites Vulkan projection-layer content on top.

## Scope

Create:

- `apps/05-passthrough`
- `libs/xr_core/include/xr_core/passthrough_interface.h`
- `libs/xr_meta_passthrough`
- `docs/passthrough.md`

Exclude: Camera2 access, passthrough styling/colour-maps, projected geometry, triangle meshes (`XR_FB_triangle_mesh`), depth occlusion, anchors, object placement. Only one reconstruction layer is created.

## Architecture

```
libs/xr_meta_passthrough/               apps/05-passthrough/
├── include/xr_meta_passthrough/        ├── CMakeLists.txt
│   └── meta_passthrough_fb.h          ├── main.cpp
├── src/                                ├── README.md
│   └── meta_passthrough_fb.cpp         ├── src/main/
└── CMakeLists.txt                      │   ├── AndroidManifest.xml
                                        │   └── cpp/
libs/xr_core/                               └── main.cpp
├── include/xr_core/                    └── build.gradle
│   ├── passthrough_interface.h (new)
│   └── xr_underlay_provider.h (new)    docs/
└── src/                                └── passthrough.md
    └── xr_session.cpp     (extended — multi-layer support)
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext        (extended — now accepts optional extension list)
  │     └── enables XR_FB_passthrough when requested
  ├── VulkanSessionBinding     (unchanged)
  ├── XrSessionContext         (extended — multi-layer frame submission)
  │     ├── PumpFrame()        (modified — supports underlay + projection layers)
  │     ├── underlayProviders_ (new — list of XrUnderlayProvider*)
  │     ├── eventObservers_    (new — list of XrEventObserver* for passthrough events)
  │     └── shouldRender_      (refined — false while Android paused)
  ├── MetaPassthroughFB        (new, libs/xr_meta_passthrough)
  │     ├── implements XrPassthroughInterface
  │     ├── implements XrUnderlayProvider
  │     ├── implements XrEventObserver
  │     ├── creates 1 passthrough feature + 1 reconstruction layer
  │     ├── start/pause/resume lifecycle
  │     └── handles XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB
  ├── VulkanSwapchain          (from M2)
  ├── VulkanPipeline           (from M2/M3)
  │     └── clear colour → transparent black (0,0,0,0)
  ├── XrMath types             (from M3)
  └── Render loop
        ├── xrWaitFrame
        ├── xrBeginFrame (only if shouldRender)
        ├── locate head + views
        ├── render wireframe cube (from M4) at (0, eyeHeight, -2)
        ├── render world axes (from M3)
        ├── collect layers: [passthrough underlay, projection layer]
        └── xrEndFrame
```

### Layer submission stack

```
┌─────────────────────────────────────┐
│  Vulkan projection layer            │  ← rendered content (cube, axes)
│  flags: BLEND_TEXTURE_SOURCE_ALPHA   │
│         UNPREMULTIPLIED_ALPHA        │
├─────────────────────────────────────┤
│  Passthrough reconstruction layer   │  ← camera feed as background
│  flags: BLEND_TEXTURE_SOURCE_ALPHA   │
│  space: XR_NULL_HANDLE              │
└─────────────────────────────────────┘
   XR_ENVIRONMENT_BLEND_MODE_OPAQUE    ← Meta compositor blends internally
```

## Portability approach

- **`XrPassthroughInterface`** — a pure-virtual C++ interface in `libs/xr_core/`. Declares `Initialize()`, `SetActive(bool)`, `Shutdown()`. The interface knows nothing about Meta extensions; it is completely runtime-agnostic.
- **`XrUnderlayProvider`** — a separate pure-virtual interface. Declares `AppendUnderlay(std::vector<CompositionLayer>& layers)`. Decouples passthrough layer submission from session frame logic.
- **`XrEventObserver`** — receives OpenXR event buffers so passthrough can handle `XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB` without polluting `XrSessionContext`.
- **`MetaPassthroughFB`** — the Meta-specific implementation of all three interfaces, living in `libs/xr_meta_passthrough/`. Loads `xrCreatePassthroughFB` etc. via `xrGetInstanceProcAddr`. Guards everything behind `#if defined(XR_FB_passthrough)`.
- **Instance extension gating** — `XrInstanceContext` is extended with a `SetRequestedExtensions(std::span<const char*>)` method. `XR_FB_passthrough` is requested by app 05 only. If the runtime doesn't support it, initialization fails with a clear log message and the app exits.
- **No styling** — M5 does not use `XrPassthroughStyleFB` or `xrPassthroughLayerSetStyleFB`. Full-opacity passthrough only. Styling is deferred past M5.

## Component breakdown

### `passthrough_interface.h` (new, in `libs/xr_core`)

```cpp
class XrPassthroughInterface {
public:
    virtual ~XrPassthroughInterface() = default;
    virtual bool Initialize(XrInstance, XrSession) = 0;
    virtual void SetActive(bool active) = 0;
    virtual void Shutdown() = 0;
};

class XrUnderlayProvider {
public:
    virtual ~XrUnderlayProvider() = default;
    virtual void AppendUnderlay(
        std::vector<XrCompositionLayerBaseHeader*>& layers) = 0;
};

class XrEventObserver {
public:
    virtual ~XrEventObserver() = default;
    virtual bool HandleEvent(const XrEventDataBuffer& event) = 0;
};
```

### `meta_passthrough_fb.h / .cpp` (new, in `libs/xr_meta_passthrough`)

| Responsibility | Detail |
|----------------|--------|
| **Function pointers** | Load `xrCreatePassthroughFB`, `xrDestroyPassthroughFB`, `xrPassthroughStartFB`, `xrPassthroughPauseFB`, `xrCreatePassthroughLayerFB`, `xrDestroyPassthroughLayerFB`, `xrPassthroughLayerPauseFB`, `xrPassthroughLayerResumeFB` via `xrGetInstanceProcAddr` at init. |
| **Query capabilities** | `xrGetSystemProperties` with `XrSystemPassthroughProperties2FB` chained. Log `maxHeight`/`maxWidth` and `flags` (e.g. `XR_PASSTHROUGH_CAPABILITY_BIT_FB`). Fail if capability bit is absent. |
| **Feature + layer creation** | `xrCreatePassthroughFB(session, &createInfo, &passthrough_)` → `xrCreatePassthroughLayerFB(session, &layerInfo{passthrough_, RECONSTRUCTION_FB}, &layer_)`. Exactly one feature, one layer. |
| **Lifecycle** | `SetActive(true)` → `xrPassthroughStartFB` + `xrPassthroughLayerResumeFB`. `SetActive(false)` → `xrPassthroughLayerPauseFB` + `xrPassthroughPauseFB`. Called from main loop: active when Android resumed AND session running. |
| **Event handling** | `HandleEvent()` catches `XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB`. Log state. On `XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB`, destroy and recreate the layer. On `XR_PASSTHROUGH_STATE_CHANGED_RECOVERABLE_ERROR_BIT_FB`, log and retry. On `XR_PASSTHROUGH_STATE_CHANGED_FATAL_ERROR_BIT_FB`, signal exit. |
| **Layer submission** | `AppendUnderlay(layers)` pushes a `XrCompositionLayerPassthroughFB{layer_, BLEND_TEXTURE_SOURCE_ALPHA_BIT, XR_NULL_HANDLE}` onto the layer vector. |
| **Shutdown** | `xrDestroyPassthroughLayerFB(layer_)` then `xrDestroyPassthroughFB(passthrough_)`. Order: layer before feature. |

### `xr_session.cpp` extensions

| Change | Detail |
|--------|--------|
| **Multi-layer PumpFrame** | Replace single `XrCompositionLayerProjection` stack variable with `std::vector<const XrCompositionLayerBaseHeader*> layers`. Collect layers: first call `AppendUnderlay` on all registered underlay providers, then append projection layer. |
| **Underlay provider registry** | `RegisterUnderlayProvider(XrUnderlayProvider*)` / `UnregisterUnderlayProvider()`. |
| **Event observer registry** | `RegisterEventObserver(XrEventObserver*)` / `UnregisterEventObserver()`. PollEvents dispatches to all observers after internal handling. |
| **shouldRender flag** | `shouldRender_` already exists (`IsRunning()`). Refined: false when Android `APP_CMD_PAUSE` received AND passthrough is not active. Passthrough-only sessions could still submit the passthrough layer even while paused (deferred past M5). |
| **Zero-layer fallback** | When `shouldRender == false`, submit `layerCount = 0`. |

### `xr_instance.cpp` extensions

| Change | Detail |
|--------|--------|
| **Requested extensions** | Add `std::vector<const char*> requestedExtensions_` member. `SetRequestedExtensions()` populates it. `Initialize()` concatenates required extensions (`XR_KHR_ANDROID_CREATE_INSTANCE`, `XR_KHR_VULKAN_ENABLE2`) with requested ones before calling `xrCreateInstance`. |
| **Application name** | Add `SetApplicationName(const char*)` so each app reports its own name instead of `"OpenXR Bootstrap"`. |
| **Backward compatibility** | Default behaviour unchanged. Apps 01–04 build and run without modification. |

### Vulkan rendering changes

| Change | Detail |
|--------|--------|
| **Clear colour** | Swapchain clear colour changes from opaque `{0.015, 0.025, 0.055, 1.0}` to transparent black `{0.0, 0.0, 0.0, 0.0}` when passthrough mode is enabled. Controlled by a `VulkanRendererConfig::transparentClear` flag. |
| **Projection layer flags** | Add `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` and `XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT` to the projection layer flags. |
| **Virtual object alpha** | Fragment shader outputs `alpha = 1.0` for all rendered geometry (cube, axes). Passthrough shows through only where no geometry covers the pixel. |

## Build system

- `libs/xr_meta_passthrough/CMakeLists.txt` — static library `xr_meta_passthrough`, links `xr_core` (for interface headers), `OpenXR::openxr_loader`. Compiles with `XR_USE_PLATFORM_ANDROID`.
- `libs/xr_core/CMakeLists.txt` — add `passthrough_interface.h` to headers (no new .cpp needed; interfaces are header-only).
- `apps/05-passthrough/CMakeLists.txt` — shared module `passthrough_mr`, links `xr_core`, `vulkan_renderer`, `xr_math`, `xr_meta_passthrough`, `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.
- `apps/05-passthrough/build.gradle` — package `com.olibartfast.questlab.passthrough`. Library name `passthrough_mr`. Label `Passthrough MR`.
- `apps/05-passthrough/src/main/AndroidManifest.xml` — NativeActivity, immersive HMD, vulkan feature, PLUS:
  ```xml
  <uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />
  ```
- `settings.gradle` — add `include(":apps:05-passthrough")`.
- `scripts/build_deploy.sh` — add `--app 05-passthrough` option.

## App flow (`main.cpp`)

```
Android entry → attach JNI
→ XrInstanceContext instance;
    instance.SetApplicationName("Passthrough MR");
    instance.SetRequestedExtensions({"XR_FB_passthrough"});
    instance.Initialize(vm, activity)   // enables XR_FB_passthrough at instance level
→ VulkanSessionBinding vulkanBinding;
    vulkanBinding.Initialize(instance)  // Vulkan device + queue + command pool
→ XrSessionContext session;
    session.Initialize(instance.Instance(), instance.SystemId(),
                       vulkanBinding.GraphicsBinding())
→ MetaPassthroughFB passthrough;
    passthrough.Initialize(instance.Instance(), session.Session())
    → queries XrSystemPassthroughProperties2FB
    → creates passthrough feature + reconstruction layer
→ session.RegisterUnderlayProvider(&passthrough);
→ session.RegisterEventObserver(&passthrough);
→ VulkanSwapchain swapchains;
    swapchains.Initialize(session, vulkanBinding, transparentClear=true)
→ VulkanPipeline pipeline;
    pipeline.Initialize(swapchains, transparentClear=true)
→ wireframe cube model matrix at (0, 1.65, -2.0)

→ event loop:
    poll Android:
        APP_CMD_RESUME → passthrough.SetActive(true)
        APP_CMD_PAUSE  → passthrough.SetActive(false)
    poll OpenXR events:
        → session.PollEvents() dispatches to passthrough.HandleEvent()

    when session running AND android resumed:
        xrWaitFrame → predictedDisplayTime
        xrBeginFrame
        xrLocateViews → head pose + per-eye view/proj
        render frame (stereo):
            clear to transparent black {0,0,0,0}
            draw world axes at origin (M3)
            draw wireframe cube at (0, 1.65, -2.0) (M4 debug shapes)
        layers = session.CollectLayers(projectionLayer)
            → passthrough.AppendUnderlay(layers)   // underlay first
            → layers += projectionLayer             // content on top
        xrEndFrame(layers, OPAQUE blend mode)

→ shutdown:
    pipeline.Shutdown()
    swapchains.Shutdown()
    passthrough.Shutdown()   // layer → feature
    session.Shutdown()
    vulkanBinding.Shutdown()
    instance.Shutdown()
```

## `docs/passthrough.md`

Document covering:

1. **`XR_FB_passthrough` overview**: what it provides, what it doesn't (no camera access, no per-pixel depth).
2. **Architecture**: three-interface design (`XrPassthroughInterface`, `XrUnderlayProvider`, `XrEventObserver`), why decoupled.
3. **Layer ordering**: passthrough underlay (`XR_NULL_HANDLE` space) → Vulkan projection layer. Why `BLEND_TEXTURE_SOURCE_ALPHA_BIT` is needed on both layers.
4. **Blend mode**: why `XR_ENVIRONMENT_BLEND_MODE_OPAQUE` is required by the Meta compositor even with passthrough visible.
5. **Lifecycle**: Android resume/pause → `SetActive(true/false)`. Session state transitions (`VISIBLE`/`FOCUSED` → start passthrough; `STOPPING` → pause passthrough).
6. **Passthrough events**: state changed (restored, reinit-required, recoverable error, fatal error) and how each is handled.
7. **Capability detection**: `XrSystemPassthroughProperties2FB` query, manifest feature declaration.
8. **Transparent rendering**: clear colour rationale, alpha blending flags, why virtual objects use alpha=1.0.
9. **Performance**: single reconstruction layer, pause when not visible, 72 Hz target on Quest 3/3S.
10. **Known limitations**: no depth ordering (passthrough is always background), no per-pixel depth occlusion, no Camera2.

## Definition of Done

1. `apps/05-passthrough` builds, installs, and launches on Quest 3.
2. Camera passthrough is visible as the background; the real-world environment is clearly seen through the headset.
3. A bright wireframe cube is rendered ~2m ahead at initial eye height, stable in LOCAL space.
4. World coordinate axes (red X, green Y, blue Z) are rendered at origin and remain stable.
5. Virtual content is fully opaque (alpha=1.0) against transparent-clear background.
6. Passthrough pauses when the Android activity pauses (dashboard open) and resumes when the activity resumes.
7. Passthrough pauses when the XR session stops and resumes when the session restarts.
8. `adb logcat -s PassthroughMR` shows passthrough feature creation, capability flags, layer creation, and state-change events.
9. Three clean launch/quit cycles without crashes or leaked handles.
10. No OpenXR or Vulkan validation errors.
11. Apps 01–04 continue to build and run without regressions.
12. `docs/passthrough.md` covers all 10 sections listed above.

## Risks & Open Questions

- **`XR_ENVIRONMENT_BLEND_MODE_OPAQUE` with passthrough**: Meta's compositor treats this blend mode specially — it does NOT mean "no passthrough". The compositor internally blends the passthrough underlay with the projection layer. Other runtimes may behave differently. This is documented in `docs/passthrough.md`.
- **Passthrough reinitialization**: `XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB` is documented as rare but can occur after display changes. The plan handles it by destroying and recreating the passthrough layer. If the event fires repeatedly in a short time, add a retry-limit guard (3 attempts per session).
- **Transparent clear + depth**: Currently the Vulkan renderer has no depth buffer (wireframe and axes are simple line draws). When opaque objects are later added (M6), ensure they write alpha=1.0 to maintain VR compositing correctness.
- **`xr_core` static library**: If the `libs/xr_core/CMakeLists.txt` static library target hasn't been created by M3/M4, it must be created now. The `XrInstanceContext` extension (requested extensions) and `XrSessionContext` extension (underlay providers, event observers) make inline-per-app compilation unsustainable.
- **Instance extension ordering**: After M5, every app in the repo will use different extension sets. The `SetRequestedExtensions()` approach must be tested that apps 01–04 still build without modification (empty requested-extensions list = default behaviour).
- **Passthrough + controller input**: M5 deliberately excludes controller input. The cube is static. Controller interaction arrives in M4 and can be merged into the passthrough app as a follow-up after both M4 and M5 are individually accepted.
- **Build regression check**: The legacy `XrPassthrough` app uses `XR_FB_passthrough` directly with OpenGL ES. The new `apps/05-passthrough` uses Vulkan. Both must continue to build. No shared code between them; `libs/xr_meta_passthrough` is new, independent code.
- **Performance at 72 Hz**: Passthrough adds a compositor layer but no application-side GPU cost beyond the existing line rendering. The primary perf concern is ensuring the Android main thread is not blocked during `xrWaitFrame` while passthrough is paused.
