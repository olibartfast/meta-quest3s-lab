# Milestone 6 — Stable Spatial Object

## Goal

Place a virtual object in the physical environment via controller ray, keep it stable in LOCAL space while the user moves, and support reset/repositioning. Passthrough is composited behind rendered content so the user sees both the real world and their placed object.

## Scope

Create:

- `apps/06-spatial-object`
- No new shared libraries required — M6 reuses `xr_core`, `vulkan_renderer`, `xr_meta_passthrough`, `xr_math`, `xr_interaction`

Exclude: hand tracking, spatial anchors, floor-plane detection, room mesh, multi-object placement.

## Architecture

```
apps/06-spatial-object/
├── CMakeLists.txt
├── build.gradle
├── README.md
└── src/main/
    ├── AndroidManifest.xml     (NativeActivity, passthrough feature, vulkan)
    └── cpp/
        └── main.cpp            (app entry + SpatialScene class)

No new libraries — all dependencies already exist:
    xr_core          → instance, session, controller actions, passthrough interfaces
    vulkan_renderer  → VulkanStereoRenderer, DebugLineDraw shapes
    xr_meta_passthrough → MetaPassthroughFB (camera passthrough underlay)
    xr_math          → Vec3, Quat, Pose, Mat4, conversion helpers
    xr_interaction  → Ray, IntersectRayAabb, SelectionState (for trigger debouncing)
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext       (passthrough extension requested)
  ├── VulkanSessionBinding
  ├── XrSessionContext
  │     └── PumpFrame(renderer, &updater, &passthrough)
  │           // updater advances controller state
  │           // passthrough provides underlay
  ├── MetaPassthroughFB       (camera underlay)
  ├── XrControllerActions     (implements XrFrameUpdater — grip/aim poses, trigger)
  │     └── bound as updater in PumpFrame
  ├── SpatialScene            (implements VulkanSceneProvider)
  │     ├── In BuildScene():
  │     │   └── reads controller aim pose from XrControllerActions
  │     │   └── applies trigger hysteresis via SelectionState
  │     │   └── renders preview or placed object + controller rays
  │     └── Internal state:
  │           placed_ : bool (has the user placed the object?)
  │           placedPose_ : Pose (world-space pose when placed)
  │           selection_ : SelectionState (trigger schmitt trigger)
  ├── VulkanSwapchain         (transparent-clear mode)
  ├── VulkanPipeline
  └── Render loop
        ├── Poll Android events
        ├── Poll OpenXR events + passthrough lifecycle
        ├── PumpFrame (updater=controller, underlay=passthrough)
        ├── render frame:
        │     clear → transparent black (0,0,0,0)
        │     render controller rays (both hands, scaled to kRayLength)
        │     render preview cube OR placed cube (at placedPose_)
        │     render small axes at object position (shows world alignment)
        ├── layers: [passthrough underlay, projection layer]
        └── xrEndFrame
```

### Layer submission

Same stack as M5:

```
┌─────────────────────────────────────┐
│  Vulkan projection layer            │  ← controller rays + cube + axes
│  flags: BLEND_TEXTURE_SOURCE_ALPHA   │
├─────────────────────────────────────┤
│  Passthrough reconstruction layer   │  ← camera feed
│  space: XR_NULL_HANDLE              │
└─────────────────────────────────────┘
   XR_ENVIRONMENT_BLEND_MODE_OPAQUE
```

## Interaction behaviour

M6 has two interaction states driven by the right controller:

### State: Preview (placed_ == false)

1. Right controller aim ray is drawn as a white `DebugLineShape::Ray` scaled to `kRayLength` (3.0m).
2. A wireframe cube (`DebugLineShape::Box`, 0.25m, cyan) is drawn at the ray tip:
   ```cpp
   previewCenter_ = aim.position + aim.forward * kRayLength;
   ```
   The cube moves in real-time with the controller.
3. When the user pulls the trigger past the hysteresis threshold (0.75):
   - The cube is **placed** at the current preview center position.
   - `placed_ = true`, `placedPosition_ = previewCenter_`.
   - A short haptic pulse confirms placement.
   - State transitions to **Placed**.

### State: Placed (placed_ == true)

1. The placed cube remains at its fixed `placedPosition_` in LOCAL space. As the user walks around, the cube stays put — stabilized by being rendered in LOCAL coordinates.
2. The cube color changes to green (opaque `{0.1, 1.0, 0.2, 1.0}`) to indicate it is placed.
3. Controller rays continue to be drawn (both hands) so the user sees their hands.
4. Small world axes (`DebugLineShape::Axes`, 0.20m) are drawn at the object position.
5. When the user presses the **primary button** (A/X):
   - `placed_ = false` (return to preview mode).
   - Haptic pulse confirms the reset.
   - The cube disappears from its fixed position; the preview cube reappears at the ray tip.

### Recentering

The Meta Quest runtime handles recentering natively (long-press Oculus button resets `LOCAL` space to current head position + orientation). Since the placed object is stored in LOCAL space, it moves with the origin shift — this is the correct behavior and requires no application code. The small world axes at the placed position visually confirm the LOCAL coordinate frame.

### Frame timing

Every second (1'000'000'000 ns), log average frame time:

```
Frames: N, Avg   frame time: X.XXX ms
```

## App flow (main.cpp)

```
Android entry → attach JNI
→ XrInstanceContext instance;
    instance.Initialize(vm, activity, {"Spatial Object", 1, {XR_FB_PASSTHROUGH_EXTENSION_NAME}})
→ VulkanSessionBinding vulkanBinding;
    vulkanBinding.Initialize(instance)
→ XrSessionContext session;
    session.Initialize(instance.Instance(), instance.SystemId(), vulkanBinding.GraphicsBinding())
→ assert session.BlendMode() == XR_ENVIRONMENT_BLEND_MODE_OPAQUE
→ MetaPassthroughFB passthrough;
    passthrough.Initialize(instance.Instance(), instance.SystemId(), session.Session())
→ XrControllerActions controller;
    controller.Initialize(instance.Instance(), session.Session())
→ SpatialScene scene(&controller);
→ VulkanStereoRenderer renderer;
    renderer.Initialize(instance.Instance(), session, vulkanBinding.DeviceContext(),
                        &scene, {.transparentBackground = true})

→ event loop:
    poll Android: RESUME → passthrough.SetActive(true); PAUSE → passthrough.SetActive(false)
    poll OpenXR events + passthrough lifecycle
    when running AND resumed:
        PumpFrame(&renderer, &controller, &passthrough)
            → updater=controller  → XrControllerActions::UpdateFrame()
            → underlay=passthrough → MetaPassthroughFB::AppendUnderlay()
        renderer calls scene.BuildScene() each eye which reads controller state

→ shutdown:
    renderer.Shutdown()
    controller.Shutdown()
    passthrough.Shutdown()
    session.Shutdown()
    vulkanBinding.Shutdown()
    instance.Shutdown()
```

## Key design details

### `SpatialScene` class

```cpp
class SpatialScene final : public questlab::VulkanSceneProvider {
public:
    explicit SpatialScene(questlab::XrControllerActions* actions);

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override;

private:
    questlab::XrControllerActions* actions_;          // non-owning
    questlab::interaction::SelectionState selection_; // trigger hysteresis
    bool placed_ = false;
    questlab::math::Vec3 placedPosition_{};           // world-space (LOCAL) position
    questlab::math::Vec3 previewPosition_{};          // follows ray tip
    // frame timing
    uint64_t frameCount_ = 0;
    XrTime lastTimingLog_ = 0;
};

// Constants
constexpr float kRayLength = 3.0F;           // aim ray visual + placement distance
constexpr float kCubeSize = 0.25F;           // placed cube side length
constexpr float kPreviewSize = 0.20F;        // preview cube slightly smaller
constexpr float kTriggerThreshold = 0.75F;   // Schmitt press threshold
constexpr float kReleaseThreshold = 0.55F;   // Schmitt release threshold
```

### Right-controller vs both-controllers

For placement, only the **right** controller trigger is used (the default dominant hand). Both controllers' aim rays and grip axes are still drawn for spatial awareness. This avoids ambiguity about which hand "owns" the placement.

### Trigger debouncing

Uses the same `SelectionState` hysteresis as M4 with `pressThreshold = 0.75, releaseThreshold = 0.55`. The Schmitt trigger prevents rapid place/clear toggling. The selection-and-place logic is simpler than M4 (no spatial target intersection) — we simply use the rising edge of the trigger to capture the current preview position.

### Cube color coding

| State | Preview cube | Placed cube | Ray color |
|-------|-------------|-------------|-----------|
| Preview (not placed) | Cyan `{0.0, 0.85, 1.0, 1.0}` | — | White `{1.0, 1.0, 1.0, 1.0}` |
| Placed | — | Green `{0.1, 1.0, 0.2, 1.0}` (opaque) | White (both hands) |

### Render order

Draws are pushed in back-to-front order (the hardware renderer has no depth buffer, so draw order determines occlusion):

1. Controller grip axes (both hands, small: 0.06m) — at grip pose
2. Controller aim rays (both hands, scaled to 3.0m)  — at aim pose
3. Preview cube (if not placed) — at ray-endpoint position
4. Placed cube (if placed) — at stored LOCAL position
5. World axes (if placed) — below the placed cube (offset -0.25 Y)

All mesh/model matrices must be in LOCAL space, since `XrFrameRenderInfo` provides the view matrix that transforms LOCAL → VIEW → CLIP.

## Build system

- `apps/06-spatial-object/CMakeLists.txt` — shared module `spatial_object`, links `xr_core`, `vulkan_renderer`, `xr_math`, `xr_meta_passthrough`, `xr_interaction`, `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.
- `apps/06-spatial-object/build.gradle` — package `com.olibartfast.questlab.spatial`. Library name `spatial_object`. Label `Spatial Object`.
- `apps/06-spatial-object/src/main/AndroidManifest.xml` — NativeActivity, immersive HMD, vulkan feature, PLUS:
  ```xml
  <uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />
  ```
- `settings.gradle` — add `include(":apps:06-spatial-object")`.
- `scripts/build_deploy.sh` — add `--app 06-spatial-object` option.

## Backward compatibility

No changes to existing libraries. M6 is purely an app addition. The `XrFrameUpdater` interface already exists for controller integration, and `XrPumpFrame` already accepts all three parameters (`renderer`, `updater`, `underlayProvider`).

## Definition of Done

1. `apps/06-spatial-object` builds, installs, and launches on Quest 3.
2. Camera passthrough is visible as the background.
3. Right controller aim ray is drawn as a white line forward from the controller.
4. A cyan wireframe preview cube follows the right controller ray tip (3 meters ahead).
5. Pulling the right trigger past ~0.75 places the cube at that position in LOCAL space with a confirming haptic pulse.
6. The placed cube turns green and remains world-stable — walking around or turning the head does not move it relative to the room.
7. Small RGB axes appear below the placed cube showing the LOCAL coordinate frame at that position.
8. Pressing the A button on the right controller clears the placement (returns to preview mode).
9. The runtime's long-press recentering moves the LOCAL origin; the placed object moves with it (correct).
10. Left controller grip and aim are also drawn (purely visual, no interaction role).
11. Frame timing is logged once per second via `adb logcat -s SpatialObject`.
12. Three clean launch/quit cycles without crashes.
13. No OpenXR or Vulkan validation errors.
14. Apps 01–05 and the legacy `XrPassthrough` app continue to build and run without regressions.

## Risks & Open Questions

- **Dual-hand controller setup**: `XrControllerActions` already supports both hands. The M4 app creates all actions at startup and queries them together. M6 inherits this. Ensure the action set includes all necessary actions (grip, aim, trigger, primary, vibration for both `user/hand/left` and `user/hand/right`).
- **Passthrough + controller interaction**: M5 runs passthrough without controllers; M4 runs controllers without passthrough. M6 merges both for the first time. The `PumpFrame(renderer, updater, underlayProvider)` three-argument overload is designed for exactly this case and was tested by M4 (updater) and M5 (underlayProvider) independently.
- **Trigger vs interaction state difference from M4**: M4 uses ray-AABB intersection to gate selection. M6 does not — the trigger simply captures the current ray endpoint position regardless of what it intersects. The `SelectionState` is used purely for its Schmitt-trigger hysteresis, not for spatial intersection. This is a simpler usage pattern.
- **STAGE space**: The STAGE space may be unavailable depending on Room Setup. M6 stores the object in LOCAL space only. If the user has a guardian boundary, the Renderer already locates `stageInLocal` — M6 can optionally log the STAGE origin but doesn't depend on it.
- **Performance at 72 Hz**: Controller state queries + line draws have negligible cost. Passthrough adds no application-side GPU overhead. Frame timing logging confirms the frame budget is met.
