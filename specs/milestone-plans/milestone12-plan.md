# Milestone 12 — Environment Depth

> **Scope status after Milestone 16:** retain this plan for occlusion, raycasts,
> and coarse scene geometry. It is no longer the assumed RF-DETR object-ranging
> source. Do not feed box-only detections into Environment Depth and call the
> result object geometry without a separately validated registration and object
> isolation method. See `docs/stereo-capability.md`.

## Goal

Acquire Meta environment depth, visualize it, convert it into metric distance,
and compare it with measured physical surfaces.

This milestone is independent of RF-DETR and camera networking.

Reference:

- [Meta OpenXR Depth API](https://developers.meta.com/horizon/documentation/native/android/mobile-depth/)

## Scope

Create:

- `apps/12-environment-depth`;
- `libs/depth_source`;
- `docs/environment-depth.md`.

Exclude:

- Camera2 RGB;
- RF-DETR;
- network transport;
- RGB/depth registration;
- object-level 3D reconstruction.

## Depth source architecture

Mirror Milestone 9's boundary:

```cpp
enum class DepthSourceKind {
    MetaEnvironmentDepth,
    Replay,
    ExternalRgbd,
};

class IDepthSource {
public:
    virtual ~IDepthSource() = default;

    virtual DepthCapabilities GetCapabilities() const = 0;
    virtual bool Start(const DepthStreamConfig& config) = 0;
    virtual bool TryConsumeLatest(DepthCapture* capture) = 0;
    virtual DepthSourceStats GetStats() const = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IDepthSource> CreateDepthSource(
    const DepthSourceConfig& config,
    const DepthPlatformContext& platform);
```

`MetaEnvironmentDepthAdapter` translates OpenXR depth images and metadata into
portable, owned `DepthCapture` records. `ReplayDepthAdapter` supports host
geometry tests. `ExternalRgbd` is a reserved future factory kind.

OpenXR handles, swapchain indices, and graphics textures remain private to the
Meta adapter. The portable record contains owned/reduced depth values, metric
conversion metadata, timestamp, projection/FOV, and source pose.

## Atomic implementation sequence

### Sequence 0 — Create the app shell

1. Create app 12 from app 05.
2. Rename all application identifiers.
3. Register Gradle, deploy, CI, and APK artifact entries.
4. Build and launch passthrough.

Gate: app 12 is a clean passthrough baseline.

### Sequence 1 — Add permission and capability gates

1. Define portable depth capabilities and capture metadata.
2. Define `IDepthSource`.
3. Define `DepthSourceConfig`.
4. Implement `CreateDepthSource`.
5. Create `MetaEnvironmentDepthAdapter`.
6. Declare spatial-data permission.
7. Request it at runtime.
8. Enable `XR_META_environment_depth`.
9. Query `supportsEnvironmentDepth`.
10. Query hand-removal support separately.
11. Resolve every required extension function inside the adapter.
12. Fail to a visible unsupported state if any mandatory gate is absent.

Gate: supported, denied, and unsupported states are distinguishable, and no
OpenXR depth handle escapes the adapter.

### Sequence 2 — Own the provider lifecycle

1. Create one environment-depth provider.
2. Create one readable depth swapchain.
3. Query its width and height.
4. Enumerate graphics-specific images.
5. Start only while the OpenXR session is running.
6. Stop on session stopping.
7. Destroy the swapchain before the provider.
8. Handle pause/resume without stale handles.

Gate: three lifecycle cycles complete without OpenXR errors.

### Sequence 3 — Acquire depth metadata and images

1. Acquire at most once per OpenXR frame.
2. Acquire only between `xrBeginFrame` and `xrEndFrame`.
3. Request the pose in a documented reference space.
4. Save swapchain index.
5. Save near and far values.
6. Save per-view FOV and pose.
7. Save requested display time.
8. Reject an invalid or unavailable image.
9. Never retain a swapchain image beyond its valid ownership.

Gate: logs show coherent metadata and changing valid frames.

### Sequence 4 — Convert to metric distance

1. Document the depth texture convention.
2. Build the projection matrix from supplied FOV.
3. Handle infinite `farZ`.
4. Convert test depth values to metric distance.
5. Add host tests for projection and inverse projection.
6. Reject NaN, infinity, and non-positive distance.
7. Reject or flag values inside 0.2 metres.
8. Validate representative pixels against measured surfaces.

Gate: metric probes are plausible and math tests pass.

### Sequence 5 — Visualize depth

1. Consume depth only through `IDepthSource`.
2. Add a depth diagnostic quad or colour overlay.
3. Map valid distance to a documented colour ramp.
4. Mark invalid pixels distinctly.
5. Add a centre and controller-ray distance probe.
6. Display/log pose and image age.
7. Keep readback and conversion off the critical render path where possible.

Gate: the user can distinguish near and far physical surfaces and read a
metric probe.

### Sequence 6 — Accuracy and regression

1. Measure several surfaces between near and far operating distances.
2. Repeat measurements to expose jitter.
3. Record absolute and relative error.
4. Test hands present and absent.
5. Test slow head motion.
6. Run for 15 minutes.
7. Build earlier apps.
8. Run host tests.

Gate: `docs/environment-depth.md` contains measured results and limitations.

## Definition of Done

- app 12 acquires live Meta environment depth;
- Meta environment depth is isolated in `MetaEnvironmentDepthAdapter`;
- the app depends only on `IDepthSource`;
- replay and future external RGB-D depth can use the same contract;
- provider and swapchain lifecycle are correct;
- supplied FOV, pose, near/far, and time metadata are preserved;
- depth values convert into plausible metric distances;
- a diagnostic visualization and probe work on Quest;
- measured error and the near-field limitation are documented;
- RF-DETR, Camera2 RGB, and 3D boxes are not required.
