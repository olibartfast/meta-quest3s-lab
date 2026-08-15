# Milestone 16 — Stereo Capability Probe

## Goal

Determine, on real Quest 3/3S hardware, whether the two passthrough RGB cameras
are exposed through Camera2 as a *usable synchronized stereo pair*, and emit a
machine-readable capability report whose verdict is evaluated by host code
rather than by reading logs.

This milestone measures a platform property. It builds no perception capability
and renders nothing in 3D.

## Why this exists, and why it is sequenced early

The `zed-yolo` sample is often read as "YOLO produces 3D". It does not. The flow
is `grab()` a synchronized stereo pair, run the detector on the left image, push
the 2D boxes back with `ingestCustomBoxObjects()`, and let
`retrieveCustomObjects()` return metric position, a 3D box, velocity, and a
persistent ID. The 3D comes from the SDK: calibrated stereo, depth registered to
the *same* image, sensor synchronization, camera tracking, and object tracking.
The custom detector only supplies the 2D region.

Milestones 12–14 attempt the same result from a different, weaker base: Camera2
RGB fused with Meta environment depth. Two properties of that base are
structural, not implementation defects:

- The RGB frame and the depth map are not guaranteed to describe the same
  instant or the same pose. Meta documents that a depth map carries its own time,
  FOV, and pose and must be reprojected with them, because it is produced for
  occlusion and raycast rather than for measurement of a specific RGB frame.
- A detection box is not a mask. Without segmentation, walls, floors, and table
  surfaces inside the box contaminate any depth statistic computed over it.

Milestone 15 already lists "replace box-only depth sampling with
segmentation-assisted fusion if the measured geometry requires it" as a
contingency. If Camera2 can hand us a synchronized stereo pair, the better move
is not to patch that fusion but to build the ZED-like base underneath it:
stereo depth computed from the *same* pair the detector saw, at the *same*
instant, in the *same* frame.

That question is cheap to answer and expensive to assume. The probe needs only
Milestone 9's camera access — no depth, no detector, no transport, no
inference — and its answer changes the design of Milestones 13 and 14 before
they are implemented. **It therefore runs before Milestone 12**, ahead of any
further investment in the RGB-plus-environment-depth path.

## Scope

Create:

- `apps/16-stereo-probe` — an on-device diagnostic that enumerates, opens,
  measures, and writes one report;
- `tools/stereo_probe_report/` — a host evaluator that parses a report and
  produces the pass/partial/fail verdict, with unit tests over synthetic
  reports;
- `scripts/pull_stereo_probe.sh` — pull the report off the headset;
- `docs/stereo-capability.md` — the recorded result, the verdict, and the
  decision taken.

Reuse without redesign:

- Milestone 9 permission handling (`android.permission.CAMERA` and
  `horizonos.permission.HEADSET_CAMERA`), the Meta metadata keys
  `com.meta.extra_metadata.camera_source` and `camera_position`, and the
  Camera2 thread and lifecycle ownership already proven in
  `QuestCameraActivity`.

Exclude:

- rectification, disparity, or any depth computation;
- detection, tracking, or inference;
- any change to `IRgbCameraSource`. The probe answers whether a stereo source
  is worth defining; it does not define one.

## Privacy

The probe records metadata, timestamps, exposure values, and per-frame
bookkeeping only. It must never write pixels, thumbnails, or derived images to
disk. Frames are acquired, timestamped, and closed. This keeps the milestone
outside the fixture privacy-review workflow entirely, and that exemption is
only valid as long as no pixel ever reaches storage.

## What is probed

### Group A — Topology and calibration (no camera opened)

1. `CameraManager.getCameraIdList()`, and for every ID the Meta camera source
   and position. Record which passthrough cameras exist and their positions.
2. Whether any device advertises
   `REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA`, and if so its
   `getPhysicalCameraIds()`. Record whether the passthrough cameras appear as
   top-level IDs, as physical sub-IDs of a logical device, or both.
3. `CameraManager.getConcurrentCameraIds()`. Record whether the two passthrough
   IDs appear together in a concurrent set. This is the fallback path when no
   logical multi-camera device exists.
4. `LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE`, distinguishing `CALIBRATED`,
   `APPROXIMATE`, and absent. `CALIBRATED` is the only value that promises
   hardware-level alignment; `APPROXIMATE` is best-effort and must be measured,
   not trusted.
5. For both cameras: `LENS_INTRINSIC_CALIBRATION`, `LENS_DISTORTION`,
   `LENS_POSE_ROTATION`, `LENS_POSE_TRANSLATION`, `LENS_POSE_REFERENCE`, and
   `SENSOR_INFO_TIMESTAMP_SOURCE`. `LENS_POSE_REFERENCE` decides whether the two
   translations are expressed in a shared frame; without that, no baseline can
   be derived, whatever the numbers look like.
6. Derived baseline: the magnitude of the difference of the two pose
   translations, compared against a tape-measured physical spacing between the
   two camera windows. Record both. Disagreement above 5 mm is reported as a
   calibration-interpretation failure, not silently averaged away — the same
   caution `docs/quest-camera.md` already applies to these fields, which are an
   axis mapping plus an optical-centre offset rather than an ordinary rigid pose.
7. Per-camera `YUV_420_888` output sizes, and their intersection. A stereo pair
   requires a size both cameras support.

### Group B — Simultaneous capture (cameras opened)

8. Configure two `YUV_420_888` streams of the same intersected size, using
   physical-camera stream configuration if a logical device exists, otherwise
   two concurrently opened devices. Record which mechanism worked, and whether
   the configuration was reported as mandatory-supported before being attempted.
9. Run a repeating request and collect at least 300 matched pairs. Pair frames
   by nearest sensor timestamp with a bounded window, and count frames that
   never find a partner.
10. Report the pair skew distribution from `Image.getTimestamp()`: min, median,
    p95, max, plus per-camera inter-frame interval and jitter, sustained frame
    rate, and dropped-frame counts.
11. Record whether both timestamp sources are `REALTIME`. This is needed to pair
    frames at all, and it also settles the timebase question Milestone 13 raises.
12. Photometric parity: per-frame `SENSOR_EXPOSURE_TIME` and
    `SENSOR_SENSITIVITY` for both cameras, the active AE mode, and whether AE
    can be locked or driven identically on both. ZED delivers a pair captured
    with matched exposure; two cameras running independent AE produce a
    brightness mismatch that degrades stereo matching before any algorithm is
    chosen.

## Thresholds

Skew must be judged against what it costs, not against a round number. A skew
of `Δt` during angular motion `ω` displaces the two images by `ω·Δt`. At a
modest 30 °/s of head motion, and a 1280-pixel-wide image over roughly 80° of
horizontal FOV — about 16 px/°, to be replaced by the measured intrinsics from
Group A — the induced disparity error is:

| Skew | Misalignment at 30 °/s | Verdict |
|---|---|---|
| ≤ 1 ms | ~0.5 px | stereo-grade, including moving scenes |
| ≤ 5 ms | ~2.4 px | static and slow scenes only |
| > 5 ms, or unbounded tail | ≥ 2.4 px and growing | fail |

The evaluator separates properties that only the platform can provide from
calibration that can be reconstructed offline:

- **P1A** both passthrough cameras enumerable, with distinct positions, a common
  output size, and a logical or concurrent mechanism. This is the
  non-surrogable platform topology gate;
- **P1B** complete factory intrinsics, distortion, and compatible pose metadata,
  with a baseline consistent with measured spacing. A P1B failure is a
  per-device calibration debt, not proof that stereo is impossible;
- **P2** both streams configurable and running simultaneously at a common
  `YUV_420_888` size, producing at least 300 pairs, sustaining at least 10
  matched pairs per second, with unmatched frames below 1%;
- **P3** reported sensor-timestamp median skew ≤ 1 ms and p95 ≤ 5 ms, with no
  unbounded max;
- **P3O** physical exposure synchronization is certified by a `CALIBRATED`
  logical-camera sync type or independently validated with an optical timing
  test. Equal Camera2 timestamps alone do not satisfy P3O;
- **P4** exposure and sensitivity either matched by the driver or forceable to
  matched values.

`PASS` requires every gate. `PASS_WITH_DEBT` requires P1A, P2, P3, and P4 but
allows missing P1B or P3O; it authorizes only the bounded calibration and
optical-sync work package, not a production stereo-depth backend. `PARTIAL`
means the pair may be usable only for a documented static-scene scope. Failure
of P1A or P2, or unbounded timing, is a hard fail.

## Atomic implementation sequence

### Sequence 0 — Report contract first

1. Define the report schema (versioned, explicit, one file).
2. Implement `tools/stereo_probe_report` against it with synthetic reports
   covering pass, partial, each individual failure, and a truncated report.
3. Gate: the verdict is computed and tested on the host with no headset
   attached, so the device run produces evidence rather than a judgement call.

### Sequence 1 — Group A only

1. Create app 16 from app 09, keeping permissions and lifecycle, dropping
   capture, conversion, rendering, and the fixture recorder.
2. Enumerate, read every characteristic in Group A, write the report, exit.
3. Gate: the report is pulled, the evaluator parses it, and topology and
   factory-calibration availability are known before any camera is opened. A
   P1A failure ends the milestone here. A P1B failure is retained as explicit
   calibration debt and does not suppress the simultaneous-capture test.

### Sequence 2 — Group B

1. Open the pair by whichever mechanism Group A indicated.
2. Collect 300+ pairs, close every image immediately, write the measurements.
3. Gate: bounded memory across the run, and no leaked `Image` or camera handle.

### Sequence 3 — Verdict and decision

1. Run the evaluator on the device report.
2. Record verdict, raw numbers, and device build in `docs/stereo-capability.md`.
3. Apply the decision below, and amend the ROADMAP in the same change.

## Decision gate

- **Pass** — build a ZED-like stereo backend for RF-DETR: synchronized pair,
  detector on the left image, stereo depth from the same pair, object isolation
  inside each box, metric 3D box in camera frame, then the camera pose at
  capture time into `LOCAL`. Milestones 13 and 14 are rewritten against that
  base; Milestone 12 keeps only its independent value as an occlusion and
  raycast source.
- **Pass with debt** — source the calibration model from the Meta Passthrough
  Camera API (Camera2 intrinsics and lens-pose extrinsics) and first run a
  bounded target-free validation of that model plus the optical sync
  validation; no manual ChArUco/checkerboard procedure is planned. Proceed to
  the ZED-like backend only if rectification, metric scale, and physical
  exposure timing pass. The persisted calibration artifact is device-specific
  and must not be shipped as a universal Quest constant.
- **Partial** — a stereo backend restricted to static or slowly moving objects,
  adopted only as an explicit, documented scope reduction. It is still a better
  base than environment-depth fusion, because both images share one instant by
  construction.
- **Fail** — stop pursuing metric 3D on the headset. Drop box-depth fusion,
  keep Milestone 10's bearing rays as the honest on-device output, and move
  object pose to a server-side estimator.

## Known limits of the ZED-like target

Even a full pass buys metric position, a 3D bounding box, velocity, and a
persistent ID. It does not buy a semantic 6DoF pose: knowing which way a bottle,
a tool, or a box is actually facing still needs a dedicated pose estimator
(FoundationPose, MegaPose, or a class-specific model) downstream of stereo.
This milestone decides where the geometry comes from, not whether orientation is
solved.

Meta's Dynamic Object Tracker is not a substitute for any of this: it currently
tracks keyboards and accepts no custom detections.

## Definition of Done

App 16 runs on Quest 3/3S, writes one pixel-free capability report, and the host
evaluator turns that report into a pass, pass-with-debt, partial, or fail verdict against the
stated thresholds. `docs/stereo-capability.md` records the measured numbers, the
verdict, and the resulting decision, and the ROADMAP is amended to match. An
absent or unmeasurable capability is reported as a fail with evidence, never
worked around with an assumed synchronization.

A custom calibration remains valid only while the camera geometry and stream
mapping stay stable. Dynamic distortion correction, crop/ROI changes, firmware
changes, or mechanical movement require drift detection and recalibration.

## References

- [ZED custom object detection](https://www.stereolabs.com/docs/development/zed-sdk/modules/object-detection/custom-object-detection)
- [Android Camera2 multi-camera](https://developer.android.com/media/camera/camera2/multi-camera)
- [`LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE`](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE)
- [Meta Passthrough Camera API overview](https://developers.meta.com/horizon/documentation/unity/unity-pca-overview/)
- [Meta depth API: depth time, FOV, and pose](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-depth.md)
- [Meta Dynamic Object Tracker](https://developers.meta.com/horizon/llmstxt/documentation/native/android/mobile-dynamic-object-tracker.md)
