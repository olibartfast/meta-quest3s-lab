# Milestone 7 — Hand Tracking

## Goal

Track and visualize both hands with standard OpenXR joints, then use a
thumb/index pinch to place, grab, move, and release one spatial object over
passthrough.

## Scope

Create:

- `libs/xr_hand_tracking`
- `apps/07-hand-tracking`
- `docs/hand-tracking.md`

Extend:

- `libs/xr_math` with a tested direction-to-direction rotation helper.
- `libs/xr_interaction` with host-tested pinch hysteresis.

Reuse the Milestone 6 passthrough, frame, renderer, placement-feedback, and
cadence patterns.

Exclude hand meshes, Meta hand aim, controller fallback, simultaneous
hands/controllers, system gestures, two-hand scaling or rotation, depth
occlusion, spatial anchors, and persistence across sessions.

## 0. Close the Milestone 6 baseline

Before implementation:

- Merge the Milestone 6 branch.
- Confirm master CI builds apps 01–06 and the legacy app.
- Record the successful Quest placement, reposition, reset, passthrough,
  lifecycle, and frame-cadence checks.

Milestone 7 should build on the accepted app 06 behavior rather than copy an
unmerged implementation.

## 1. Add portable hand-tracking state

Create `XrHandTracker` in `libs/xr_hand_tracking`. It implements
`XrFrameUpdater`, owns one `XrHandTrackerEXT` for each hand, and exposes a
renderer-friendly state:

```cpp
enum class HandSide : std::size_t {
    Left = 0,
    Right = 1,
};

struct HandJointState {
    math::Pose pose;
    float radius = 0.0F;
    bool positionValid = false;
    bool orientationValid = false;
    bool positionTracked = false;
    bool orientationTracked = false;
};

struct HandState {
    bool active = false;
    std::array<HandJointState, XR_HAND_JOINT_COUNT_EXT> joints;
};
```

Keep raw OpenXR handles, function pointers, and output structures private.
The public state uses repository math types and explicit validity flags so
application and rendering code do not depend on Meta-specific structures.

`XrHandTracker::Initialize` takes the instance, system ID, and session. It
must:

1. Query `XrSystemHandTrackingPropertiesEXT` with
   `xrGetSystemProperties`.
2. Fail clearly when `supportsHandTracking` is false.
3. Resolve `xrCreateHandTrackerEXT`, `xrLocateHandJointsEXT`, and
   `xrDestroyHandTrackerEXT` through `xrGetInstanceProcAddr`.
4. Create left and right trackers with
   `XR_HAND_JOINT_SET_DEFAULT_EXT`.
5. Destroy any partially created resources if initialization fails.

The library depends on `xr_core` because it implements `XrFrameUpdater`; a
link only to `xr_math` and the loader is insufficient.

References:

- [Khronos `XR_EXT_hand_tracking`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_hand_tracking.html)
- [Khronos hand-tracking system properties](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSystemHandTrackingPropertiesEXT.html)

## 2. Locate joints at predicted display time

On every `UpdateFrame`:

- Call `xrLocateHandJointsEXT` once per tracker.
- Use `frame.baseSpace`, which is the session's existing `LOCAL` space.
- Use `frame.predictedDisplayTime`.
- Supply exactly `XR_HAND_JOINT_COUNT_EXT` locations.
- Copy pose, radius, and location flags into `HandState`.

Use `XrHandJointLocationsEXT::isActive` as the hand-level tracking state.
There is no aggregate `HAND_TRACKING_TRACKED_BIT` in
`XR_EXT_hand_tracking`.

When `isActive` is false, clear every joint and release any pinch owned by
that hand. When active, the extension requires every joint to have valid
position and orientation, but the implementation should still copy and
check each joint's valid and tracked bits. Valid-but-untracked joints may be
rendered with reduced emphasis, but interaction requires tracked thumb and
index positions.

Reference:
[Khronos hand-joint location semantics](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandJointLocationsEXT.html)

## 3. Add host-tested pinch detection

Extend `xr_interaction` with a small OpenXR-independent `PinchState`.
Inputs are optional tracked thumb-tip and index-tip positions. Output
contains:

```cpp
struct PinchFrameResult {
    bool active = false;
    bool started = false;
    bool ended = false;
    float distance = 0.0F;
    math::Vec3 center;
};
```

Use distance hysteresis:

- Start at or below 0.025 m.
- End at or above 0.040 m.
- Compute the center as the midpoint of the two tips.
- Clear and emit `ended` when either required tip loses tracked position.

Keep the thresholds explicit and device-tunable. Do not label a linear
thumb/index distance mapping as runtime-provided pinch strength.

Host tests cover:

- Open hand remains inactive.
- Crossing the press threshold emits one rising edge.
- Holding emits no duplicate edge.
- Crossing the release threshold emits one falling edge.
- Tracking loss releases safely.
- Left and right state machines remain independent.

This standard-joint implementation is intentionally portable.
`XR_FB_hand_tracking_aim` and its runtime-filtered pinch state remain a
future Meta-specific backend, not a Milestone 7 dependency.

## 4. Render a lightweight skeleton

Render a line skeleton using the existing `DebugLineShape::Ray`. Define one
static table of 25 parent/child pairs:

- Wrist to palm.
- Wrist through the four thumb joints.
- Palm through each five-joint index, middle, ring, and little-finger chain.

Add:

```cpp
bool RotationFromTo(
    const Vec3& from,
    const Vec3& to,
    Quat* rotation);
```

It normalizes both inputs, returns false for zero-length or non-finite
directions, and handles parallel and anti-parallel vectors explicitly. This
lets a unit `-Z` ray be translated, rotated, and scaled between two joint
positions. Add math tests for parallel, anti-parallel, perpendicular,
zero-length, and arbitrary directions.

For each bone:

- Require valid parent and child positions.
- Skip zero-length or non-finite segments.
- Use cyan for the left hand and white for the right.
- Dim segments whose positions are valid but not currently tracked.

Draw small axes only at the wrist and five fingertips. Do not draw axes at
all 26 joints; that would obscure the skeleton and add unnecessary draws.

## 5. Add one-hand-at-a-time object manipulation

`HandTrackingScene` implements both `XrFrameUpdater` and
`VulkanSceneProvider`, owns `XrHandTracker` plus one `PinchState` per hand,
and stores one object pose in `LOCAL`.

Interaction:

1. Show the same large yellow startup guide used by app 06.
2. Show a small cyan marker at each active pinch center.
3. If no object is placed, the first pinch places it at that hand's pinch
   center.
4. If an object is placed, a pinch beginning within 0.15 m grabs it.
5. While held, update the object position from the owning pinch center.
6. Releasing the pinch or losing that hand drops the object at its last
   valid pose.
7. If both hands start a pinch in the same frame, prefer the right hand for
   deterministic ownership.

The object is green while placed and yellow while held. The non-owning hand
continues to render but cannot steal an active grab.

Do not add a two-second pinch reset: it conflicts with ordinary
pinch-and-hold manipulation and is not required by the roadmap. App relaunch
is sufficient for the milestone; a later gesture/UI design can add an
unambiguous reset.

## 6. Compose with passthrough

Follow app 06:

- Request `XR_FB_passthrough` and `XR_EXT_hand_tracking`.
- Initialize `MetaPassthroughFB`.
- Use a transparent Vulkan background.
- Poll passthrough events and mirror Android/session activation.
- Pump the renderer, hand-tracking scene, and passthrough provider together.

Submit:

```text
Meta passthrough reconstruction underlay
Vulkan projection layer
```

Continue using `LOCAL` for joints and the object. Document that this is
in-session world locking, not anchor persistence, and that system recentering
can relocate stored coordinates.

## 7. Add manifest and application integration

Meta requires both declarations for hand input:

```xml
<uses-permission android:name="com.oculus.permission.HAND_TRACKING" />
<uses-feature
    android:name="oculus.software.handtracking"
    android:required="true" />
```

Also retain the passthrough feature from app 06. Do not add high-frequency
tracking metadata until baseline device measurements show it is necessary.
Document Meta's restriction that estimated hand size and pose data are used
only to provide the app's hand-tracking interaction.

Reference:
[Meta hand-tracking enablement](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-hand-tracking.md)

Application configuration:

- ID: `com.olibartfast.questlab.handtracking`
- Label and OpenXR application name: `Hand Tracking`
- Native library: `hand_tracking`
- Log tag: `HandTracking`

Add app 07 to `settings.gradle`, `scripts/build_deploy.sh`, and Android CI,
including Gradle cache input, build step, and APK artifact upload.

Deployment:

```bash
./scripts/build_deploy.sh --app 07-hand-tracking
adb logcat -s HandTracking:V OpenXR:V '*:S'
```

## Verification

- Run ShellCheck and strict toolchain diagnostics.
- Run `xr_math`, `xr_interaction`, and any host-safe hand-state tests.
- Build apps 01–07 and the legacy `XrPassthrough` target.
- Inspect APK metadata for package, label, NativeActivity library,
  passthrough feature, hand-tracking feature, permission, and ARM64 ABI.
- With controllers set aside and hand tracking enabled, verify both hands
  activate and all finger chains follow motion.
- Verify either hand can place the object.
- Verify near-object pinch, hold, movement, release, and tracking-loss drop.
- Verify no false pinch fires when a hand enters or leaves tracking.
- Open and close the Meta dashboard; skeletons must disappear while input is
  inactive and recover afterward.
- Check frame cadence at the default runtime refresh rate.
- Perform three clean launch/quit cycles and inspect OpenXR/Vulkan errors.

## Acceptance

- Passthrough and an immediate yellow guide make successful startup visible.
- Left and right hand skeletons render from standard OpenXR joints.
- Inactive hands and invalid joints disappear without stale geometry.
- Pinch edges are stable and do not repeat while held.
- Either hand can place one object without controllers.
- A nearby pinch grabs, moves, and drops the object.
- Tracking loss releases a grab safely.
- The placed object remains visually stable during normal movement.
- Dashboard pause/resume restores passthrough and hand tracking.
- Frame cadence remains suitable for the active Quest refresh rate.
- Apps 01–07 and the legacy app build without regression.
- Three device launch/quit cycles complete without OpenXR or Vulkan errors.
