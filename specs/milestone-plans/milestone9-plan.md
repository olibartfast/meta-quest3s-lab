# Milestone 9 — Quest Camera Capture

## Goal

Access one forward-facing RGB camera on Quest 3 from the native application,
show its frames on a Vulkan diagnostic quad, and create a small opt-in replay
fixture.

This milestone introduces no RF-DETR, network transport, environment depth,
or 3D reconstruction. Its only new platform risk is Camera2/Camera NDK.

References:

- [Meta Passthrough Camera API overview](https://developers.meta.com/horizon/documentation/unity/unity-pca-overview/)
- [Meta Android Native Camera2 API](https://developers.meta.com/horizon/documentation/native/android/pca-native-documentation/)

## Scope

Create:

- `apps/09-quest-camera`;
- `libs/camera_source`;
- `docs/quest-camera.md`;
- one privacy-reviewed replay fixture and manifest.

Reuse:

- app 05 passthrough lifecycle;
- the native Vulkan renderer;
- existing Android lifecycle and logging infrastructure.

Exclude:

- model export or inference;
- network transport;
- environment depth;
- RGB/depth synchronization;
- object detection or 3D overlays;
- continuous recording;
- background capture.

## Fixed decisions

1. Enumerate cameras; never hard-code a camera ID.
2. Select one passthrough camera by Meta source and position metadata.
3. Select an explicitly supported YUV420 resolution.
4. Preserve image plane row and pixel strides.
5. Keep at most a small bounded number of owned frames.
6. Drop old frames instead of blocking the OpenXR frame loop.
7. Require an explicit developer action before writing any image.
8. Write captures only to app-private storage.
9. Keep Android Camera2 types private to the Meta adapter.
10. Select the source through a factory; application and renderer code must
    not branch on the concrete backend.

## Camera source architecture

Use both the Adapter and Factory patterns:

```cpp
enum class CameraSourceKind {
    MetaCamera2,
    Replay,
    ExternalRgbd,
};

struct CameraSourceConfig {
    CameraSourceKind kind = CameraSourceKind::MetaCamera2;
    std::string replayManifestPath;
};

class IRgbCameraSource {
public:
    virtual ~IRgbCameraSource() = default;

    virtual CameraCapabilities GetCapabilities() const = 0;
    virtual bool Start(const CameraStreamConfig& config) = 0;
    virtual bool TryConsumeLatest(RgbCapture* capture) = 0;
    virtual CameraSourceStats GetStats() const = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IRgbCameraSource> CreateCameraSource(
    const CameraSourceConfig& config,
    const CameraPlatformContext& platform);
```

Initial adapters:

- `MetaCamera2Adapter`: translates Camera2/Camera NDK callbacks, metadata,
  formats, and lifetimes into repository-owned `RgbCapture` objects;
- `ReplayCameraAdapter`: reads the versioned fixture and publishes the same
  `RgbCapture` contract with deterministic timing.

`ExternalRgbdAdapter` is an explicit future factory kind, not an M9
implementation requirement. Its RGB stream will implement
`IRgbCameraSource`. A later sensor-rig abstraction may pair that RGB source
with an `IDepthSource`; do not put optional depth fields into `RgbCapture`.

The public contract owns portable data only:

- frame ID and timestamps;
- dimensions and pixel format;
- owned plane bytes and strides;
- camera intrinsics and distortion;
- a documented camera-to-rig/head pose;
- source capability and health information.

It exposes no `ACamera*`, JNI, Android image, OpenXR, Vulkan, or external
camera SDK type.

## Atomic implementation sequence

Complete each gate before starting the next sequence.

### Sequence 0 — Preserve the baseline

1. Record unrelated working-tree changes.
2. Build apps 05 and 08.
3. Run existing host tests.
4. Launch app 05 once on Quest.
5. Save a clean passthrough lifecycle log.

Gate: the existing passthrough path is known-good.

### Sequence 1 — Create the camera app shell

1. Create app 09 from the minimum app 05 structure.
2. Rename package, target, native library, label, and log tag.
3. Register app 09 in Gradle.
4. Register app 09 in `scripts/build_deploy.sh`.
5. Add its CI build and APK artifact.
6. Build the APK.
7. Install and launch it.
8. Confirm passthrough and one diagnostic cube.

Gate: app 09 is a working passthrough application with no camera code.

### Sequence 2 — Add permission state

1. Declare `android.permission.CAMERA`.
2. Declare `horizonos.permission.HEADSET_CAMERA`.
3. Declare `android.hardware.camera2.any`.
4. Add a native-callable runtime permission helper.
5. Represent `Pending`, `Granted`, and `Denied` explicitly.
6. Request permission only after the activity can present a prompt.
7. Handle denial without terminating OpenXR.
8. Log one state transition per actual change.

Gate: grant and denial paths are both repeatable on Quest.

### Sequence 3 — Enumerate camera capabilities

1. Define portable camera capabilities and stream configuration.
2. Define `IRgbCameraSource`.
3. Define `CameraSourceConfig`.
4. Implement `CreateCameraSource`.
5. Create `MetaCamera2Adapter` after permission is granted.
6. Enumerate all camera IDs inside that adapter.
7. Read Meta camera-source metadata.
8. Read Meta left/right position metadata.
9. Enumerate YUV output sizes.
10. Query intrinsics, distortion, and lens pose when exposed.
11. Select one camera deterministically.
12. Select one tested supported resolution.
13. Translate all results into portable capability types.
14. Log the complete selected configuration once.
15. Return a precise unsupported-capability state if selection fails.

Gate: logs identify a real Quest passthrough camera without a hard-coded ID,
and no Camera2 type escapes the adapter.

### Sequence 4 — Capture owned YUV frames

1. Add RAII wrappers for camera resources.
2. Create a bounded image reader.
3. Create the capture request and session.
4. Start capture only while the activity is resumed.
5. Acquire images on a non-render callback path.
6. Validate image dimensions and three-plane layout.
7. Respect each plane's row and pixel stride.
8. Copy or retain image memory with explicit ownership.
9. Assign a monotonic `frameId`.
10. Record sensor and callback-arrival timestamps.
11. Publish through a bounded latest-frame queue.
12. Count overwritten and invalid frames.
13. Stop capture before destroying referenced resources.

Gate: a 15-minute capture run has bounded memory and no render-loop stall.

### Sequence 5 — Display a diagnostic preview

1. Add a YUV-to-RGB conversion path.
2. Test conversion with a host-side stride fixture.
3. Upload only the latest eligible frame to Vulkan.
4. Render it on a head-relative diagnostic quad.
5. Preserve the source aspect ratio.
6. Handle texture recreation after resolution changes.
7. Show a visible unavailable/denied state.
8. Log conversion and upload time once per second.

Gate: the headset shows a correctly oriented, correctly coloured live camera
preview without disturbing passthrough cadence.

### Sequence 6 — Add explicit capture and replay

1. Define a versioned capture manifest.
2. Store frame ID, timestamps, dimensions, strides, format, and calibration.
3. Add an explicit developer capture action.
4. Limit each action to a small fixed frame count.
5. Write to app-private storage atomically.
6. Log the exact saved path and byte count.
7. Pull one capture with ADB.
8. Review it for private content.
9. Create a sanitized repository fixture only after approval.
10. Implement `ReplayCameraAdapter`.
11. Register it in `CreateCameraSource`.
12. Run the existing preview unchanged with the replay source.
13. Verify replay preserves pixels and metadata.
14. Verify switching sources changes only factory configuration.

Gate: recording never starts automatically and the approved fixture replays
deterministically through the same application path.

### Sequence 7 — Lifecycle and regression acceptance

1. Test permission denial.
2. Test permission grant.
3. Test three pause/resume cycles.
4. Test three launch/exit cycles.
5. Test capture start failure.
6. Test camera disconnect or forced close where practical.
7. Run the 15-minute preview test.
8. Build apps 01–09.
9. Build the legacy passthrough app.
10. Run host tests.

Gate: all Definition of Done evidence is recorded in the app README.

## Required host tests

- camera metadata selection;
- factory selection and unsupported source kind;
- Meta adapter capability translation;
- unsupported configuration handling;
- YUV row/pixel-stride conversion;
- frame ownership after source release;
- bounded queue overwrite;
- capture manifest round trip;
- replay adapter contract equivalence;
- shutdown with and without frames.

## Definition of Done

- app 09 captures a real Quest forward-facing camera;
- the live preview is correctly coloured and oriented;
- permission denial remains safe and visible;
- queues and memory remain bounded;
- pause/resume and shutdown release camera resources correctly;
- timestamps and calibration metadata are recorded;
- one explicit, privacy-reviewed fixture replays deterministically;
- Meta Camera2 and replay use the same `IRgbCameraSource` interface;
- switching those sources requires only factory configuration;
- application and renderer code contain no Camera2-specific branches;
- the interface can accept a future external RGB-D camera's RGB adapter
  without changing consumers;
- no inference, networking, or depth implementation is required;
- earlier apps and legacy passthrough still build.
