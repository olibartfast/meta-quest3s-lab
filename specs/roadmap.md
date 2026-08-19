# Roadmap

Phases 1–8 below cover work that is not yet done. Milestones 0–10 and 16 are implemented
on disk; what remains of them is headset acceptance, which is Phase 1. The full milestone
history and rationale live in `ROADMAP.md`, and the per-milestone implementation plans live
in `specs/milestone-plans/`; this file is the executable backlog.

## Phase 1 — Close the Device Acceptance Backlog

Nothing downstream can be trusted until the implemented milestones are verified on real
hardware. Each item below is a headset run, not a code change.

- Verify Milestone 1 (`apps/01-openxr-bootstrap`):
  - Three clean launch/exit cycles with no leaked OpenXR handles in `adb logcat`
  - One regression launch of the preserved `XrPassthrough` baseline
- Verify Milestone 2 (`apps/02-vulkan-stereo-triangle`):
  - Stable stereo geometry with correct per-eye projection
  - No persistent OpenXR or Vulkan validation errors with `questVulkanValidation=true`
- Verify Milestone 3 (`apps/03-head-pose`):
  - World-locked reference object while HMD pose changes
  - Host tests for transform composition and inversion in `libs/xr_math`
- Verify Milestone 6 (`apps/06-spatial-object`): placement, recenter, reposition, and
  frame-timing logs across a full session
- Verify Milestone 7 (`apps/07-hand-tracking`): both-hand visualization, pinch selection,
  graceful partial and lost tracking, lifecycle cycles
- Verify Milestone 8 (`apps/08-spatial-anchors`): create, persist, restore, and failure
  paths against the current Quest runtime; record actual extension behavior in
  `docs/spatial-anchors.md`
- Verify Milestone 9 (`apps/09-quest-camera`):
  - Live preview from a real passthrough camera on the diagnostic quad
  - Correct metadata logging: timestamps, intrinsics, lens pose, stream format
  - Pause/resume survival and a long-run capture without unbounded queue growth
  - Record the privacy-reviewed RGB fixture and its manifest through the explicit
    developer action only
  - Deterministic replay of one approved frame through `ReplayCameraAdapter`
- Verify Milestone 10 (`apps/10-rfdetr-detection`):
  - Agreement with the host oracle on the approved fixture, within the tolerances in
    `tools/rfdetr_export/model-manifest.json` (`comparison` block)
  - 15-minute thermal run per backend, on-device and streaming
  - Measured OpenXR frame phases unchanged while inference runs
  - Select and document the default backend in `docs/rfdetr-detection.md`
- Update each milestone's `Status:` line in `ROADMAP.md` from "acceptance pending" to the
  measured result

## Phase 2 — Stereo Calibration and Optical Sync Gate

Milestone 16 returned `PASS_WITH_DEBT`. This phase pays P1B and P3O. No stereo-depth or
object-pose work may start before it passes.

- Read `LENS_INTRINSIC_CALIBRATION`, `LENS_POSE_ROTATION`, and `LENS_POSE_TRANSLATION`
  for both passthrough cameras through the Camera2 path in `libs/camera_source`
- Build the per-device stereo calibration model from those API values; do not implement a
  manual target procedure
- Validate the model in software on live scenes:
  - Reprojection consistency across the pair
  - Rectified vertical disparity, measured and bounded
- Close P3O: measure the optical synchronization relationship between left and right
  exposures and record the result in `docs/stereo-capability.md`
- Persist the validated model as a per-device artifact, checksummed through
  `libs/artifact_integrity`
- Record an explicit gate verdict:
  - **Pass** → Phase 3 proceeds
  - **Fail** → drop same-pair stereo depth, fall back to the environment-depth fusion
    app 10 already uses, and rewrite Phases 3–5 accordingly

## Phase 3 — Same-Pair Stereo Depth

Gated on Phase 2 passing.

- Add a stereo depth path that consumes the exact concurrent pair, not two nearby frames
- Rectify both images with the validated calibration
- Produce disparity and convert to metric range, rejecting invalid and near-field values
- Add host tests over recorded stereo fixtures with known geometry
- Measure range error against several physically measured surfaces and document it

## Phase 4 — Time and Pose Registration

Milestone 13, rewritten. Required for either stereo or environment-depth geometry.

- Determine and validate the Camera2 sensor timestamp timebase on Quest
- Correlate Camera2, monotonic, and OpenXR time domains; report failure as a blocker
  rather than approximating it
- Locate the HMD and RGB camera pose at capture time, not at render time
- Match each RGB frame to a bounded retained depth or stereo record
- Compose sources through a `libs/sensor_rig` factory
- Reject temporally distant or geometrically invalid pairs
- Measure alignment error while static and during controlled slow head motion
- Write `docs/quest-camera-depth-calibration.md`

## Phase 5 — Metric 3D Overlay

Milestone 14. **The mechanism already exists** — `libs/detection_fusion` and app 10
render depth-fused metric boxes today. This phase measures whether they are right.

- Decide whether the measurements run against app 10 or a separate
  `apps/14-cv-spatial-overlay`; do not rebuild what app 10 already does
- Retrieve the retained RGB/range record for each RF-DETR frame ID
- Isolate the object inside each detection; box-only depth sampling is not the production
  design
- Robustly cluster range samples and compute a metric center, conservative extents, and a
  fusion confidence
- Transform results into OpenXR `LOCAL` and render confidence- and age-coded 3D boxes
- Hide invalid, low-confidence, stale, or uncorrelated results
- Measure 2D IoU, range error, 3D center error, extent error, and jitter
- Validate against at least two real physical object classes; mock detections cannot
  satisfy completion
- Write `docs/milestone14-validation.md`

## Phase 6 — Transport Hardening

Milestone 11, rescoped. Extends `apps/10-rfdetr-detection`; no new application.

- Extract the Milestone 10 streaming adapter into `libs/perception_protocol` with explicit
  encoding, versioning, and bounded message sizes
- Harden the Milestone 10 service into `tools/rfdetr_inference_server`
- Negotiate the RF-DETR model manifest at connect and fail closed on mismatch
- Reject malformed, duplicate, unknown, and expired frame IDs
- Handle server absence, overload, disconnect, and reconnect without restarting OpenXR
- Add host codec, framing, and fault-injection tests
- Measure capture-to-result latency and queue occupancy under each injected fault

## Phase 7 — Environment Depth

Milestone 12, retained for occlusion, raycasts, and coarse scene geometry. **The adapter
already exists** in `libs/depth_source` and app 10 starts and consumes it; what is missing
is this phase's evidence.

- Create `apps/12-environment-depth` as the standalone diagnostic
- Confirm the spatial-data permission and `XR_META_environment_depth` capability check
  behave correctly when support is absent
- Acquire one depth image at the valid point in each OpenXR frame; preserve near/far, FOV,
  pose, dimensions, and display time
- Convert depth texture values to metric distance; reject the unreliable near field
- Add a depth visualization and distance probe
- Measure error for several static surfaces
- Write `docs/environment-depth.md`

## Phase 8 — Performance and Production Quality

Milestone 15. Device baselines are blocked on Phases 1–5.

- Capture device baselines for every completed app using the `benchmark` variant and
  `scripts/capture_performance_bundle.sh`
- Profile capture, depth acquisition, readback, transport, inference, fusion, and rendering
  independently
- Evaluate RF-DETR input resolution, quantization, and runtime backends against an explicit
  accuracy/latency budget
- Add temporal association and tracking without hiding raw detector output
- Minimize frame-loop allocations; extend RAII coverage for OpenXR and Vulkan handles
- Extend CI beyond build to host unit tests, formatting, and sanitizers for host libraries
- Document Quest thermal and mobile GPU constraints
- Add screenshots or short recordings for each completed application
- Keep `docs/architecture.md` current: add depth, perception-protocol, sensor-rig, and
  fusion ownership as those phases land
