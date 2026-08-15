# Milestone 6 — Stable Spatial Object

## Goal

Place and reposition one virtual object with either Quest controller, keep its
pose fixed in `LOCAL` space while the user moves, and render it over
passthrough.

## Scope

Create:

- `apps/06-spatial-object`
- `apps/06-spatial-object/README.md`

Reuse `xr_core`, `xr_math`, `xr_interaction`, `vulkan_renderer`, and
`xr_meta_passthrough`. No new shared library is expected.

Exclude plane or mesh intersection, scene understanding, depth occlusion,
hand tracking, multiple objects, spatial anchors, and persistence across
sessions. Placement is intentionally at a fixed distance along a controller
aim ray.

## 1. Combine the Milestone 4 and 5 paths

Model the application on the existing apps rather than introducing a second
frame architecture:

- Follow app 04's scene pattern: `SpatialObjectScene` implements both
  `XrFrameUpdater` and `VulkanSceneProvider` and owns
  `XrControllerActions`.
- Follow app 05's passthrough initialization, Android lifecycle handling,
  transparent renderer configuration, and cleanup order.
- Poll OpenXR events with `MetaPassthroughFB` as the event observer.
- Activate passthrough only while Android is resumed and the OpenXR session
  is running.
- Pump frames with all three existing hooks:

```cpp
xrSession.PumpFrame(&renderer, &scene, &passthrough);
```

This is the first application to exercise controller updates, Vulkan
rendering, and a passthrough underlay in the same frame. Treat that combined
path as a primary integration risk even though each hook has already passed
device acceptance independently.

## 2. Add placement state

Keep placement state in `SpatialObjectScene`:

```cpp
bool placed;
math::Pose placedInLocal;
std::array<bool, 2> triggerPressed;
std::array<bool, 2> primaryPressed;
```

For each controller with an active, valid aim pose:

1. Rotate local `-Z` by the aim orientation and normalize it.
2. Draw the aim ray.
3. Compute a preview pose at a fixed two-metre distance:

```cpp
preview.position = aim.position + direction * 2.0F;
preview.orientation = aim.orientation;
```

4. On a trigger rising edge at `0.75`, copy that preview pose to
   `placedInLocal` and pulse the same controller.
5. Rearm only after the trigger falls to `0.55` or below.

The first trigger press places the object. A later press from either hand
repositions it directly. If both triggers cross the threshold in one update,
prefer the right controller so the result is deterministic.

An X or A rising edge clears the object and returns to preview-only state.
Clearing must not depend on a valid aim pose.

Do not use `interaction::SelectionState` for this state machine. Its trigger
edge is deliberately gated by ray/AABB intersection for Milestone 4, whereas
Milestone 6 has no physical or virtual target to intersect. Keep the small
placement edge state application-local unless implementation reveals a
second reusable consumer.

## 3. Define the stability contract

Store the captured pose directly in the renderer's existing `LOCAL`
coordinate system. Controller action spaces and `xrLocateViews` are already
located against the same `XrSessionContext::LocalSpace()`, so no extra
`XrSpace` or per-frame coordinate conversion is required.

`LOCAL` is world-locked for normal in-session movement, but it is not a
spatial anchor. A system recenter can change its natural origin. The existing
session code logs `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`; the
app should retain its stored coordinates, document the resulting physical
relocation as expected `LOCAL` behaviour, and allow immediate repositioning.

`STAGE` remains optional and is not used for placement. Spatial persistence
and stronger real-world attachment remain Milestone 8 concerns.

References:

- [Khronos `LOCAL` reference space](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_REFERENCE_SPACE_TYPE_LOCAL.html)
- [Khronos reference-space change event](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEventDataReferenceSpaceChangePending.html)

## 4. Render placement feedback over passthrough

Use only the existing debug-line shapes:

- Both valid controller grip axes and aim rays.
- A small cyan preview box at each valid aim endpoint.
- One green placed box at `placedInLocal`.
- Small RGB axes at the placed pose to expose orientation stability.

Keep virtual lines opaque, clear the Vulkan swapchain to transparent black,
and submit:

```text
Meta passthrough reconstruction underlay
Vulkan projection layer
```

Keep `XR_ENVIRONMENT_BLEND_MODE_OPAQUE`, matching app 05. Do not add a depth
buffer or imply real-world occlusion; the object is always composited over
the camera image.

Reference:
[Meta passthrough best practices](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-passthrough-bp.md)

## 5. Log frame cadence

Measure the wall-clock duration of each `PumpFrame` call with
`std::chrono::steady_clock`. Once per second, log:

- Frames completed in the interval.
- Mean `PumpFrame` duration.
- Maximum `PumpFrame` duration.

Label these values as frame cadence, not GPU render cost: the measurement
includes the runtime wait in `xrWaitFrame`. Keep logging off the per-frame
hot path except for the timestamp and accumulator updates.

## 6. Add application and build integration

Application configuration:

- ID: `com.olibartfast.questlab.spatialobject`
- Label and OpenXR application name: `Spatial Object`
- Native library: `spatial_object`
- Log tag: `SpatialObject`
- Required extension: `XR_FB_passthrough`
- Required manifest feature: `com.oculus.feature.PASSTHROUGH`

Add:

- `apps/06-spatial-object/CMakeLists.txt`
- `apps/06-spatial-object/build.gradle`
- NativeActivity manifest, strings, C++ entry point, and README
- `:apps:06-spatial-object` in `settings.gradle`
- `06-spatial-object` support in `scripts/build_deploy.sh`
- Gradle caching, build, and APK artifact coverage in Android CI

Deployment:

```bash
./scripts/build_deploy.sh --app 06-spatial-object
adb logcat -s SpatialObject:V OpenXR:V '*:S'
```

## Verification

- Run ShellCheck on the repository scripts.
- Run the `xr_math` and `xr_interaction` host tests.
- Build apps 01–06 and the legacy `XrPassthrough` regression target.
- Confirm the app manifest package, NativeActivity library, passthrough
  feature, label, and OpenXR application name agree.
- Verify `adb devices` reports exactly one authorized headset before device
  testing.
- Test placement and direct repositioning with each controller.
- Walk around and turn the head while checking that the placed pose remains
  fixed relative to the room during normal tracking.
- Open and close the Meta dashboard and verify controller visuals and
  passthrough recover.
- Perform a system recenter, confirm the reference-space-change log, and
  verify the object can be repositioned without restarting.
- Perform three launch/quit cycles and inspect logs for OpenXR or Vulkan
  errors.

## Acceptance

- Passthrough fills the background and Vulkan geometry composites above it.
- Each active controller shows a valid grip pose and aim ray.
- Cyan preview geometry follows valid aim poses at the documented fixed
  distance.
- Either trigger places or repositions one green object and produces haptic
  confirmation once per press.
- X or A clears the object once per press.
- The stored object pose remains visually stable during normal head and body
  movement and across dashboard pause/resume.
- Recenter behaviour is logged and documented accurately; no anchor-like
  persistence is claimed.
- Frame-cadence mean and maximum are logged once per second.
- Apps 01–06 and the legacy app build without regression.
- Three device launch/quit cycles complete without OpenXR or Vulkan errors.
