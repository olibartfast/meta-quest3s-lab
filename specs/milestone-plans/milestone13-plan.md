# Milestone 13 — RGB and Depth Alignment

> **Revision required after Milestone 16:** this document remains a valid
> experiment for Camera2-to-OpenXR timing and Environment Depth reprojection,
> but it is no longer automatically the primary object-ranging path. Implement
> the common time/pose-correlation pieces only after the stereo calibration and
> optical-sync gate selects the validated branch. See
> `docs/stereo-capability.md`.

## Goal

Align Quest Camera2 RGB with Meta environment depth spatially and temporally.
Display reprojected depth samples over the exact retained RGB frame and
measure pixel error.

This milestone deliberately excludes RF-DETR so calibration errors cannot be
confused with detector errors.

## Scope

Create:

- `apps/13-rgb-depth-alignment`;
- `libs/sensor_rig`;
- reusable timestamp-correlation and reprojection code;
- `docs/quest-camera-depth-calibration.md`.

Reuse:

- Milestone 9 camera-source frames, intrinsics, factory, and preview;
- Milestone 12 environment depth images and metadata.

Exclude:

- RF-DETR;
- network transport;
- object-level depth clustering;
- world-space detection boxes.

## Sensor rig composition

Add a small composition layer rather than coupling source factories:

```cpp
struct SensorRig {
    std::unique_ptr<IRgbCameraSource> rgb;
    std::unique_ptr<IDepthSource> depth;
    SensorRigCalibration calibration;
};

SensorRig CreateSensorRig(
    const SensorRigConfig& config,
    const SensorPlatformContext& platform);
```

Initial configuration:

```text
MetaQuestRig
    RGB   = MetaCamera2Adapter
    depth = MetaEnvironmentDepthAdapter
```

Future configuration:

```text
ExternalRgbdRig
    RGB   = ExternalRgbdCameraAdapter
    depth = ExternalRgbdDepthAdapter
```

The alignment application consumes `SensorRig`; it does not name Camera2,
Meta environment depth, or an external vendor SDK in its fusion code.

## Atomic implementation sequence

### Sequence 0 — Integrate without fusion

1. Define `SensorRigConfig`.
2. Define `SensorRigCalibration`.
3. Implement `CreateSensorRig`.
4. Register the `MetaQuestRig` composition.
5. Create app 13 from the camera app.
6. Consume RGB and depth through the rig.
7. Keep separate RGB and depth diagnostics.
8. Build and launch both streams.
9. Confirm neither stream changes the other's lifecycle behavior.

Gate: RGB and depth run simultaneously without registration code, and the app
contains no concrete Meta source class.

### Sequence 1 — Establish timestamp domains

1. Record the source-provided sensor timestamp.
2. Record camera callback-arrival monotonic time.
3. Record OpenXR predicted display time.
4. Convert OpenXR time to the platform monotonic domain.
5. Identify the Meta adapter's sensor timestamp timebase on the target OS.
6. Measure offset and drift between time domains.
7. Document uncertainty.
8. Reject unsupported assumptions.

Gate: the timestamp mapping has measured offset, drift, and uncertainty. If it
cannot be established, stop rather than silently using callback time.

An external RGB-D adapter may supply a different timebase and hardware-
synchronized depth. Timestamp conversion therefore belongs to source metadata
and correlation policy, not to application-specific Camera2 branches.

### Sequence 2 — Capture the RGB camera pose

1. Read camera intrinsics.
2. Read distortion coefficients and convention.
3. Read camera lens pose relative to the HMD.
4. Locate the HMD at RGB capture time.
5. Compose `localFromHead * headFromCamera`.
6. Test transform inversion and composition on the host.
7. Log pose validity flags.
8. Reject frames without required pose validity.

Gate: every eligible RGB frame has a documented `localFromCamera`.

### Sequence 3 — Retain bounded correlation records

1. Copy or reduce depth data before swapchain ownership ends.
2. Store depth FOV, pose, near/far, and timestamp.
3. Maintain a bounded RGB metadata ring.
4. Maintain a bounded depth snapshot ring.
5. Match nearest timestamps.
6. Define a maximum acceptable time delta from measurements.
7. Reject unmatched pairs.
8. Record pair delta and age.

Gate: static and slow-motion runs produce bounded, explainable pair deltas.

### Sequence 4 — Reproject depth into RGB

1. Convert a selected depth pixel into a depth-view ray.
2. Convert its texture value into metric distance.
3. Form the metric point in depth-view space.
4. Transform it into `LOCAL`.
5. Transform it into RGB camera space.
6. Reject points behind the RGB camera.
7. Project with RGB intrinsics.
8. Apply the documented distortion model.
9. Reject points outside the RGB image.
10. Add host tests with synthetic geometry.

Gate: known synthetic points project to expected RGB pixels.

### Sequence 5 — Visualize alignment

1. Select a bounded sparse set of depth pixels.
2. Reproject them into the retained RGB frame.
3. Colour samples by metric distance.
4. Draw them on the exact correlated preview frame.
5. Show pair time delta and rejection state.
6. Freeze one pair for close inspection.
7. Avoid drawing unmatched samples.

Gate: depth discontinuities visibly follow corresponding RGB edges.

### Sequence 6 — Measure alignment

1. Define a scene with high-contrast measured edges.
2. Capture a static-head sample.
3. Capture a slow-yaw sample.
4. Capture a slow-translation sample.
5. Annotate corresponding RGB edges.
6. Measure median and percentile reprojection error.
7. Separate spatial error from temporal-motion error.
8. Repeat after pause/resume.
9. Run for 15 minutes.
10. Build earlier apps and run host tests.

Gate: measured error and failure cases are recorded in the calibration doc.

## Definition of Done

- Camera2, monotonic, and OpenXR time relationships are measured;
- RGB and depth are selected through a sensor-rig factory;
- Meta-specific source types do not escape their adapters;
- every eligible RGB frame has a capture-time camera pose;
- depth snapshots retain the supplied view pose and projection metadata;
- depth points reproject into the correct retained RGB frame;
- unmatched or temporally distant pairs are rejected;
- static and slow-motion pixel errors are measured;
- RF-DETR and object-level 3D fusion are not required.
