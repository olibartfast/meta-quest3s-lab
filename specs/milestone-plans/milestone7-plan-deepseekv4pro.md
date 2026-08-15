# Milestone 7 — Hand Tracking

## Goal

Replace (or supplement) controller input with natural hand interaction using `XR_EXT_hand_tracking`. Visualize joint skeletons, implement pinch detection, and enable pinch-to-place spatial interaction — all while preserving passthrough compositing from M5/M6.

## Scope

Create:

- `libs/xr_hand_tracking/` — reusable hand-tracker wrapper, joint data, pinch detection
- `apps/07-hand-tracking/` — standalone app with hand skeletons, pinch interaction, passthrough
- A minor extension to `xr_math` (`RotationFromTo` quaternion helper for bone-line model matrices)

Exclude: `XR_FB_hand_tracking_mesh` (hand mesh), `XR_FB_hand_tracking_aim` (aim-pose gestures), two-hand interactions (e.g. scaling/rotating), articulated body tracking, and body/face tracking.

## Architecture

```
libs/xr_hand_tracking/                   apps/07-hand-tracking/
├── include/xr_hand_tracking/            ├── CMakeLists.txt
│   └── xr_hand_tracker.h               ├── build.gradle
├── src/                                 ├── README.md
│   └── xr_hand_tracker.cpp              └── src/main/
└── CMakeLists.txt                           ├── AndroidManifest.xml
                                             └── cpp/
                                                 └── main.cpp

No existing libraries need modification except xr_math (one new function).
```

### Component relationship

```
main.cpp
  ├── XrInstanceContext       (XR_EXT_hand_tracking requested)
  ├── VulkanSessionBinding
  ├── XrSessionContext
  │     └── PumpFrame(renderer, &updater, &passthrough)
  │           // updater queries joint poses each frame
  │           // passthrough provides camera underlay
  ├── MetaPassthroughFB       (camera underlay)
  ├── XrHandTracker           (new, in xr_hand_tracking lib)
  │     ├── Implements XrFrameUpdater
  │     ├── Creates XrHandTrackerEXT for LEFT and RIGHT
  │     ├── Locates 26 joints per hand each frame
  │     ├── Computes pinch state from thumb/index tip proximity
  │     └── Handles partial / lost tracking gracefully
  ├── HandTrackingScene       (implements VulkanSceneProvider)
  │     ├── reads joint positions from XrHandTracker
  │     ├── reads pinch state for interaction
  │     ├── draws joint axes (skeleton)
  │     └── draws placed cube + pinched cube(s)
  ├── VulkanSwapchain         (transparent-clear mode)
  ├── VulkanPipeline
  └── Render loop
        ├── Poll Android events
        ├── Poll OpenXR events + passthrough lifecycle
        ├── PumpFrame (renderer, updater=handTracker, underlay=passthrough)
        ├── render frame:
        │     clear → transparent black
        │     render joint skeletons (both hands)
        │     render placed cube (if any)
        │     render pinch preview cube (follows pinch midpoint)
        ├── layers: [passthrough underlay, projection layer]
        └── xrEndFrame
```

### Layer submission

Same as M5/M6:

```
┌─────────────────────────────────────┐
│  Vulkan projection layer            │  ← skeletons + cubes + axes
│  flags: BLEND_TEXTURE_SOURCE_ALPHA   │
├─────────────────────────────────────┤
│  Passthrough reconstruction layer   │  ← camera feed
│  space: XR_NULL_HANDLE              │
└─────────────────────────────────────┘
   XR_ENVIRONMENT_BLEND_MODE_OPAQUE
```

## Reusable Library: `libs/xr_hand_tracking/`

### `XrHandTracker` class

This is the main reusable component. It is **not** vendor-specific — `XR_EXT_hand_tracking` is a Khronos multi-vendor extension. It lives in its own library for reusability across apps.

```cpp
namespace questlab {

// Internal storage for 26 OpenXR hand joints
static constexpr uint32_t kHandJointCount = 26;

struct HandJointSet {
    XrSpaceLocationFlags locationFlags = 0;
    XrHandJointLocationEXT joints[kHandJointCount]{};
};

struct PinchResult {
    bool isPinching = false;
    bool pinchStarted = false;        // rising edge this frame
    bool pinchEnded = false;          // falling edge this frame
    float pinchStrength = 0.0F;       // 0 (open) to 1 (fully closed)
    float pinchDistance = 1.0F;       // Euclidean distance thumb→index tip
    math::Vec3 pinchCenter{};
};

class XrHandTracker final : public XrFrameUpdater {
public:
    enum class Hand { Left = 0, Right = 1 };

    bool Initialize(XrInstance instance, XrSession session);
    bool UpdateFrame(const XrFrameUpdateInfo& frame) override;
    void Shutdown();

    // Joint access. Returns nullptr if the hand is not tracked.
    const HandJointSet* Joints(Hand hand) const;

    // Convenience: get a specific joint as math::Pose. Returns std::nullopt
    // if the hand or joint is not valid.
    std::optional<math::Pose> JointPose(
        Hand hand, XrHandJointEXT joint) const;

    // Pinch state with hysteresis
    const PinchResult& Pinch(Hand hand) const;

    // Is the hand actively tracked?
    bool IsTracked(Hand hand) const;
};

}  // namespace questlab
```

### Pinch detection algorithm

Pinch is computed each frame from the Euclidean distance between:

- `XR_HAND_JOINT_THUMB_TIP_EXT` (thumb tip)
- `XR_HAND_JOINT_INDEX_TIP_EXT` (index tip)

```
thumbTip = joints[THUMB_TIP].pose.position
indexTip = joints[INDEX_TIP].pose.position
distance = Length(indexTip - thumbTip)

pinchStrength = 1.0 - clamp(distance / maxDistance, 0, 1)
    where maxDistance = 0.06m (fully open)

pinch is active when pinchStrength >= 0.75 (press threshold)
pinch releases when pinchStrength <= 0.55 (release threshold)
```

Hysteresis uses the standard Schmitt trigger pattern already established in M4/M6. The same threshold values (0.75 press, 0.55 release) provide consistency.

### Pinch midpoint

The pinch center is computed as the midpoint between thumb tip and index tip:

```cpp
pinchCenter = (thumbTip.position + indexTip.position) * 0.5
```

This position, transformed into LOCAL space, serves as the cube placement/grabbing point.

### Graceful degradation

| Condition | Behaviour |
|-----------|-----------|
| `xrCreateHandTrackerEXT` fails for one hand | That hand returns null from `Joints()`. The app draws only the working hand. |
| `xrLocateHandJointsEXT` returns partial validity | Only joints with `POSITION_VALID_BIT` + `ORIENTATION_VALID_BIT` are drawn. Untracked joints are skipped silently. |
| `HAND_TRACKING_TRACKED_BIT` absent | `IsTracked()` returns false. App draws nothing for that hand. |
| Hand leaves camera FOV mid-pinch | Pinch is released (safety guard: pinch auto-releases when tracking is lost). |
| Hand re-enters FOV | Joints resume drawing as soon as they become valid; pinch detector re-arms. |

## Vulkan Renderer Changes

### Bone-line rendering approach

Hand skeletons are rendered as chains of line segments connecting parent joints to child joints. Rather than adding new shader code, each bone is drawn using the existing `DebugLineShape::Ray` — a line from origin to `(0,0,-1)`. The model matrix transforms it to the correct world-space segment.

This requires building a model matrix per bone that:
1. Translates origin to the parent joint position
2. Rotates `(0,0,-1)` to the bone direction (child - parent)
3. Scales Z by the bone length

The rotation step needs a new `xr_math` utility:

```cpp
// In xr_math: builds a quaternion that rotates `from` to `to`
Quat RotationFromTo(const Vec3& from, const Vec3& to);
```

Per-bone model matrix construction:

```cpp
Vec3 direction = childPosition - parentPosition;
float length = Normalize(&direction);
Quat rotation = RotationFromTo({0.0F, 0.0F, -1.0F}, direction);
Mat4 model = Multiply(
    PoseMatrix({rotation, parentPosition}),
    ScaleMatrix(1.0F, 1.0F, length));
```

**Bone count**: 25 segments per hand × 2 hands = 50 extra debug-line draws per eye. One color per hand (left: cyan, right: white).

### Joint hierarchy (parent → child pairs)

The bone connections follow the OpenXR joint chain:

```
WRIST         → index_thumb       → thumb_metacarpal   → thumb_proximal   → thumb_distal    → thumb_tip
WRIST         → index_metacarpal  → index_proximal     → index_intermediate → index_distal → index_tip
WRIST         → middle_metacarpal → middle_proximal    → middle_intermediate → middle_distal → middle_tip
WRIST         → ring_metacarpal   → ring_proximal      → ring_intermediate  → ring_distal  → ring_tip
WRIST         → little_metacarpal → little_proximal    → little_intermediate → little_distal → little_tip
```

Additionally, small joint axes at each wrist (radius 0.03m) and at tips (radius 0.01m) give visual weight to joint positions. The total draw count per frame: ~50 bone lines + 10 joint axes = ~60 debug draws per eye.

### Alternative: joint-only visualization

If bone-line matrix construction proves fragile (edge case: fingers pointing exactly along world +Z, which is parallel to `(0,0,-1)`, causing degenerate rotation), fall back to drawing only small axis triads at each tracked joint. This shows joint positions and orientations clearly without per-bone matrix math. The plan includes both; the bone-line path is attempted first.

## App: `apps/07-hand-tracking/`

### Interaction behaviour

The app has two interaction states for the right hand:

#### State: No placed object

1. Hand skeletons are drawn (left: cyan, right: white).
2. The right hand's pinch midpoint is shown as a small cyan preview cube (radius 0.025m), real-time following the hand.
3. When the user pinches (rising edge, strength ≥ 0.75):
   - The cube is **placed** at the current pinch midpoint in LOCAL space.
   - Cube turns green (0.25m side, wireframe).
   - State transitions to **Placed**.

#### State: Placed object

1. The placed cube remains at its fixed LOCAL-space position.
2. Pinching again (rising edge):
   - If the pinch midpoint is near the cube (distance < 0.20m): **grab** — cube starts following the pinch midpoint each frame.
   - If the pinch midpoint is far from the cube: **relocate** — cube jumps to pinch midpoint.
3. While grabbed, releasing the pinch (falling edge) drops the cube at its current position.
4. The left hand never affects placement; its skeleton is drawn for spatial awareness only.

#### Reset

A firm right-hand pinch held for ≥ 2 seconds clears the placement and returns to preview mode. A long-pinch timer is tracked internally.

### Cube color coding

| State | Cube | Left hand skeleton | Right hand skeleton | Preview marker |
|-------|------|--------------------|--------------------|--------------------|
| No placed obj | — | Cyan | White | Small cyan box at pinch midpoint |
| Placed (idle) | Green `{0.1, 1.0, 0.2}` | Cyan | White | — |
| Grabbed (dragging) | Yellow `{1.0, 0.85, 0.0}` | Cyan | Orange `{1.0, 0.5, 0.0}` | — |

### Joint data in LOCAL space

`XR_EXT_hand_tracking` locates joints relative to a caller-supplied `XrSpace`. By passing `XrSessionContext::LocalSpace()` as the base space, all joint poses are returned directly in LOCAL coordinates — no application-side coordinate transformation needed. This matches how M3/M6 work (objects stored in LOCAL space).

### Frame timing

Same `FrameCadenceLogger` from M6, logging once per second. Tag: `HandTracking`.

## App flow (main.cpp)

```
Android entry → attach JNI
→ XrInstanceContext instance;
    instance.Initialize(vm, activity,
        {"Hand Tracking", 1, {XR_FB_PASSTHROUGH_EXTENSION_NAME,
                              XR_EXT_HAND_TRACKING_EXTENSION_NAME}})
→ VulkanSessionBinding vulkanBinding;
→ XrSessionContext session;
    session.Initialize()
→ assert session.BlendMode() == OPAQUE
→ MetaPassthroughFB passthrough;
    passthrough.Initialize()
→ XrHandTracker handTracker;                          // <-- NEW
    handTracker.Initialize(instance, session.Session())
→ HandTrackingScene scene(&handTracker);              // <-- NEW
→ VulkanStereoRenderer renderer;
    renderer.Initialize(&scene, transparentBg=true)

→ Main loop:
    poll Android events
    xrSession.PollEvents(&passthrough)
    passthrough.SetActive(resumed && running)
    xrSession.PumpFrame(&renderer, &handTracker, &passthrough)
        → handTracker.UpdateFrame()                     // locate joints
        → renderer.RenderFrame() → scene.BuildScene()   // draw skeletons
    cadenceLogger.Record(frameTime)

→ Shutdown:
    renderer → handTracker → passthrough → session → vulkanBinding → instance
```

## Build system

### `libs/xr_hand_tracking/CMakeLists.txt`

```cmake
add_library(xr_hand_tracking STATIC
    src/xr_hand_tracker.cpp
)
target_include_directories(xr_hand_tracking PUBLIC include)
target_link_libraries(xr_hand_tracking PUBLIC xr_math OpenXR::openxr_loader)
target_compile_features(xr_hand_tracking PUBLIC cxx_std_17)
```

### `apps/07-hand-tracking/CMakeLists.txt`

Shared module `hand_tracking`, links: `xr_core`, `vulkan_renderer`, `xr_math`, `xr_hand_tracking`, `xr_meta_passthrough`, `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.

### `apps/07-hand-tracking/build.gradle`

Package `com.olibartfast.questlab.handtracking`. Library name `hand_tracking`. Label `Hand Tracking`.

### `AndroidManifest.xml`

NativeActivity, immersive HMD, vulkan feature, PLUS:

```xml
<uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />
<uses-feature android:name="oculus.software.handtracking" android:required="true" />
```

The `oculus.software.handtracking` feature declaration ensures the app only launches on devices with hand tracking (Quest 2/3/3S).

## `xr_math` extension

One new function in `libs/xr_math/`:

```cpp
// Returns a quaternion that rotates the `from` direction to the `to` direction.
// Uses the half-angle method with normalised cross and dot products.
// If from and to are parallel, returns identity quaternion.
// If from and to are anti-parallel, returns a 180° rotation about any perpendicular axis.
Quat RotationFromTo(const Vec3& from, const Vec3& to);
```

Implementation follows the standard quaternion-from-two-vectors formula:
```cpp
Vec3 axis = Cross(from, to);
float dotProduct = Dot(from, to);
if (dotProduct > 0.9999F) return IdentityQuat();         // parallel
if (dotProduct < -0.9999F) {                             // anti-parallel
    Vec3 perp = (std::abs(from.x) < 0.99F)
        ? Cross(from, {1,0,0}) : Cross(from, {0,1,0});
    Normalize(&perp);
    return {perp.x, perp.y, perp.z, 0.0F};              // 180° rotation
}
Normalize(&axis);
float sinHalfAngle = std::sqrt((1.0F - dotProduct) * 0.5F);
float cosHalfAngle = std::sqrt((1.0F + dotProduct) * 0.5F);
return {axis.x * sinHalfAngle, axis.y * sinHalfAngle,
        axis.z * sinHalfAngle, cosHalfAngle};
```

## Backward compatibility

- `libs/xr_hand_tracking/` is new — no existing code depends on it.
- `xr_math` gains one new free function. No existing function signatures change.
- M7 is a standalone app. Apps 01–06 continue to build and run without modification.
- The legacy `XrPassthrough` app is unaffected.

## Definition of Done

1. `apps/07-hand-tracking` builds, installs, and launches on Quest 3.
2. Camera passthrough is visible as the background.
3. Both hand skeletons are rendered: wrist→finger chains as white (right) and cyan (left) bone lines, or axis triads at each tracked joint if the bone-line path is deferred.
4. Joint tracking is real-time: skeletons follow the user's hands at hand-tracking frame rate.
5. Pinch detection works: bringing thumb and index fingertips together (strength ≥ 0.75) triggers a pinch event logged to logcat.
6. Pinch-to-place: a right-hand pinch creates a green wireframe cube at the pinch midpoint in LOCAL space.
7. The placed cube remains world-stable — moving around it does not shift it relative to the room.
8. Pinch-within-20cm-of-cube grabs and drags it; release drops it at the new position.
9. A 2-second sustained right-hand pinch clears the placement (reset).
10. When both hands leave the camera FOV, no joints are drawn and the app does not crash. Joints reappear when hands return.
11. Frame cadence is logged once per second via `adb logcat -s HandTracking`.
12. Three clean launch/quit cycles without crashes.
13. No OpenXR or Vulkan validation errors.
14. Apps 01–06 and the legacy `XrPassthrough` app continue to build and run without regressions.

## Risks & Open Questions

- **Hand-tracking availability**: `XR_EXT_hand_tracking` must be enabled in the Quest system settings (Settings → Movement Tracking → Hand and Body Tracking). If disabled, `xrCreateHandTrackerEXT` returns `XR_ERROR_FEATURE_UNSUPPORTED`. The app must log a clear message and continue running (drawing only passthrough + prior cube state) rather than crashing.
- **Pinch vs natural hand tracking latency**: Hand tracking runs at a lower update rate than the display (typically 30–60 Hz vs 72 Hz display). The joint data may be one frame behind the head pose. This is expected and not a bug; document it.
- **Bone-line degenerate case**: When a finger points exactly along `(0,0,-1)` (straight forward in the DEFAULT grip orientation), the `RotationFromTo` with `from = (0,0,-1)` and `to = (0,0,-1)` returns identity — correct. But the `from` and `to` vectors are in LOCAL space, not grip space, so fingers will almost never align exactly with world -Z. Degenerate rotations are unlikely in practice.
- **Two-hand interactions deferred**: M7 does one-handed pinch-to-place. Two-hand pinch gestures (pinch both hands to scale the cube, pinch-and-drag with both hands to rotate) are natural extensions deferred to a follow-up milestone or post-M7 iteration.
- **XR_EXT_hand_tracking vs XR_FB_hand_tracking_mesh**: M7 uses only standard joint positions. Meta's `XR_FB_hand_tracking_mesh` provides a skinned hand mesh with per-vertex normals and indices, and `XR_FB_hand_tracking_aim` provides controller-like aim poses from hand poses. Both are powerful but vendor-specific and deferred past M7.
- **Performance budget**: 50+ debug-line draws per eye are well within budget at 72 Hz (each is a push-constant update + `vkCmdDraw` with ≤24 vertices). The dominant cost is still the Vulkan render pass overhead, not the number of draws.
- **Android manifest feature protection**: `oculus.software.handtracking` should be marked `required="true"` so the Quest Store and SideQuest filter correctly. This prevents install on devices without hand tracking support.
- **Editor integration**: The `XR_EXT_hand_tracking` extension is functional in Meta's XR Simulator, enabling desk-bound development. Document this.
