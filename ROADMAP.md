# Meta Quest 3 Native XR Roadmap

## Purpose

This repository is a native C++ XR laboratory for Meta Quest 3.

The objective is to extend an existing background in C++, computer vision, GPU programming, and inference systems toward:

- native OpenXR development;
- Vulkan-based stereo rendering;
- spatial computing;
- passthrough and mixed reality;
- hand and controller interaction;
- spatial anchors;
- integration of real-time computer-vision pipelines with XR interfaces.

The project should remain primarily native. Unity or Unreal may be used only for comparison, validation, or rapid prototyping, not as the main implementation path.

---

## Agent Instructions

When implementing work from this roadmap:

1. Preserve native C++ as the main application layer.
2. Prefer OpenXR APIs over vendor-specific abstractions whenever possible.
3. Isolate Meta-specific extensions behind small interfaces.
4. Use Vulkan for rendering.
5. Keep examples small, independently buildable, and documented.
6. Avoid copying complete SDK samples without restructuring and explaining them.
7. Add a README to every application describing:
   - what it demonstrates;
   - required OpenXR extensions;
   - build and deployment commands;
   - expected behaviour on the headset;
   - known limitations.
8. Add logging and explicit error handling for every OpenXR and Vulkan call.
9. Do not introduce Unity, Unreal, or a large third-party engine unless explicitly requested.
10. Complete one milestone before adding features from later milestones.

---

## Target Repository Structure

```text
meta-quest3-lab/
├── apps/
│   ├── 01-openxr-bootstrap/
│   ├── 02-vulkan-stereo-triangle/
│   ├── 03-head-pose/
│   ├── 04-controller-input/
│   ├── 05-passthrough/
│   ├── 06-spatial-object/
│   ├── 07-hand-tracking/
│   ├── 08-spatial-anchors/
│   ├── 09-quest-camera/
│   ├── 10-rfdetr-detection/
│   ├── 12-environment-depth/
│   ├── 13-rgb-depth-alignment/
│   ├── 14-cv-spatial-overlay/
│   └── 16-stereo-probe/
├── libs/
│   ├── xr_core/
│   ├── vulkan_renderer/
│   ├── xr_math/
│   ├── perf_telemetry/
│   ├── spatial_ui/
│   ├── camera_source/
│   ├── depth_source/
│   ├── object_detector/
│   ├── perf_telemetry/
│   ├── sensor_rig/
│   └── perception_protocol/
├── tools/
│   ├── rfdetr_export/
│   └── rfdetr_inference_server/
├── docs/
├── scripts/
├── third_party/
├── README.md
└── ROADMAP.md
```

The structure may be introduced incrementally. Do not perform a large reorganization unless the current build remains functional after each step.

---

# Milestone 0 — Stabilize the Development Environment

## Goal

Make the repository reproducibly buildable and deployable from a Linux development machine to Meta Quest 3.

## Tasks

- Validate `scripts/setup_quest_dev_env.sh`.
- Validate `scripts/udev_env_setup.sh`.
- Confirm Android SDK, Android NDK, CMake, Ninja, Java, ADB, and Gradle versions.
- Verify `adb devices` detects the headset.
- Add a command that prints the complete toolchain configuration.
- Ensure paths are configurable and are not tied to one workstation.
- Add basic troubleshooting documentation.
- Confirm at least one native OpenXR sample builds, installs, launches, and logs correctly.

## Deliverables

- Reproducible setup scripts.
- `docs/development-environment.md`.
- A verified build-and-deploy command.

## Definition of Done

A clean Linux environment can execute the documented setup, build an APK, install it through ADB, and launch it on Quest 3 without manual source edits.

---

# Milestone 1 — Native OpenXR Bootstrap

**Status:** Implemented; Quest 3 acceptance is pending. Do not mark complete until three clean launch/exit cycles and the legacy regression launch pass.

## Goal

Create the smallest application owned by this repository that correctly manages the Android and OpenXR lifecycle.

## Tasks

- Create an Android NativeActivity or equivalent native entry point.
- Initialize the Android application lifecycle.
- Create `XrInstance`.
- Enumerate and log available OpenXR extensions.
- Query `XrSystemId` for the HMD.
- Create `XrSession`.
- Implement the OpenXR event loop.
- Handle session state transitions correctly.
- Create at least one reference space.
- Shut down all resources cleanly.

## Suggested Components

```text
libs/xr_core/
├── xr_instance.*
├── xr_session.*
├── xr_extensions.*
├── xr_reference_space.*
├── xr_error.*
└── xr_logging.*
```

## Definition of Done

The application launches on Quest 3, enters an active OpenXR session, logs lifecycle transitions, and exits without validation errors or leaked OpenXR handles.

---

# Milestone 2 — Vulkan Stereo Rendering

**Status:** Implemented; Quest 3/3S acceptance is pending. Do not mark complete
until the documented visual, lifecycle, validation, and regression checks pass.

## Goal

Render repository-owned graphics correctly in both eyes using OpenXR swapchains and Vulkan.

## Tasks

- Initialize Vulkan through OpenXR graphics requirements.
- Select a compatible physical device.
- Create the Vulkan instance, device, queues, and command pools.
- Enumerate OpenXR view configuration.
- Create stereo colour swapchains.
- Implement the frame loop:
  - `xrWaitFrame`;
  - `xrBeginFrame`;
  - locate views;
  - acquire swapchain images;
  - render both eyes;
  - release swapchain images;
  - `xrEndFrame`.
- Render a triangle or simple cube.
- Add optional Vulkan validation support for development builds.

## Deliverables

- `apps/02-vulkan-stereo-triangle`.
- Reusable `libs/vulkan_renderer`.
- Documentation of the OpenXR/Vulkan frame lifecycle.

## Definition of Done

A stable stereoscopic object is visible in the headset with correct per-eye projection and no persistent OpenXR or Vulkan validation errors.

---

# Milestone 3 — Tracking and Coordinate Systems

**Status:** In progress; implementation requires host, CI, and Quest 3/3S
acceptance before completion.

## Goal

Understand and expose the spatial foundations required for XR and computer-vision integration.

## Tasks

- Support `VIEW`, `LOCAL`, and `STAGE` reference spaces where available.
- Read and log head pose.
- Visualize coordinate axes.
- Implement reusable vector, matrix, pose, and quaternion conversions.
- Document coordinate-system conventions:
  - handedness;
  - axis directions;
  - units;
  - transform multiplication order;
  - OpenXR pose semantics.
- Add tests for transform composition and inversion where practical.

## Deliverables

- `apps/03-head-pose`.
- `libs/xr_math`.
- `docs/coordinate-systems.md`.

## Definition of Done

The application displays a stable world-space reference object while separately visualizing or logging the changing HMD pose.

---

# Milestone 4 — Controller Input and Interaction

Status: complete. Quest device acceptance passed.

## Goal

Implement portable OpenXR input rather than hard-coding device-specific controller events.

## Tasks

- Create an OpenXR action set.
- Add pose, trigger, grip, thumbstick, and button actions.
- Suggest bindings for Quest Touch controllers.
- Create action spaces for both hands.
- Render controller poses or rays.
- Implement ray selection of a simple virtual object.
- Add object highlighting or selection feedback.

## Deliverables

- `apps/04-controller-input`.
- Reusable action and interaction abstractions.
- `libs/xr_interaction`.
- `docs/openxr-actions.md`.

## Definition of Done

The user can point at and select a virtual object with either controller using OpenXR actions.

---

# Milestone 5 — Passthrough Mixed Reality

Status: complete. Quest visual acceptance passed.

## Goal

Create a minimal mixed-reality application with passthrough and native Vulkan content.

## Tasks

- Detect and enable the required Meta passthrough extension.
- Isolate the vendor-specific passthrough implementation behind an interface.
- Create, start, pause, resume, and destroy passthrough resources safely.
- Submit the correct composition layers.
- Render a virtual object over passthrough.
- Handle application pause and resume.

## Deliverables

- `apps/05-passthrough`.
- `libs/xr_core/passthrough_interface.*`.
- Meta-specific implementation in a clearly named module.
- `libs/xr_meta_passthrough`.
- `docs/passthrough.md`.

## Definition of Done

The headset shows passthrough with a stable repository-owned Vulkan object composited into the scene.

---

# Milestone 6 — Stable Spatial Object

**Status:** Implemented on disk; Quest 3/3S interaction, stability, lifecycle,
and performance acceptance is pending.

## Goal

Place a virtual object in the physical environment and keep it visually stable while the user moves.

## Tasks

- Place an object relative to a `LOCAL` or `STAGE` space.
- Allow placement using a controller ray.
- Store the selected pose in application state.
- Render the object using the correct view and projection transforms.
- Add recentering and reset behaviour.
- Measure and log frame timing.

## Deliverables

- `apps/06-spatial-object`.
- A simple placement interaction.

## Definition of Done

The user can place a cube or marker in the room, move around it, and reset or reposition it without restarting the application.

---

# Milestone 7 — Hand Tracking

**Status:** Implemented on disk; Quest 3/3S hand-visualization, pinch,
tracking-loss, lifecycle, and performance acceptance is pending.

## Goal

Add natural hand interaction using OpenXR or Meta extensions while preserving a clean abstraction boundary.

## Tasks

- Detect hand-tracking support.
- Create trackers for left and right hands.
- Retrieve joint poses each frame.
- Render a lightweight joint skeleton.
- Implement pinch detection.
- Use pinch to select or move an object.
- Handle partial tracking and lost tracking gracefully.

## Deliverables

- `apps/07-hand-tracking`.
- Reusable hand-state representation.

## Definition of Done

Both hands are visualized when tracked, and a pinch gesture can trigger an interaction without controllers.

---

# Milestone 8 — Spatial Anchors

**Status:** Implemented on disk; Quest 3/3S create, persist, restore, failure,
lifecycle, and performance acceptance is pending.

## Goal

Persist or restore virtual content at meaningful physical locations when supported by the runtime.

## Tasks

- Investigate the current Quest-supported anchor extensions and permissions.
- Create an anchor from a selected world-space pose.
- Create a space associated with the anchor.
- Locate the anchor every frame.
- Save and restore anchors where the platform API permits it.
- Expose anchor lifecycle states and errors in logs or UI.

## Deliverables

- `apps/08-spatial-anchors`.
- `docs/spatial-anchors.md` documenting portable and Meta-specific behaviour.

## Definition of Done

A user can create an anchor, attach a virtual marker to it, and restore it according to the capabilities currently exposed by the Quest runtime.

---

## Computer-Vision Milestone Ladder

```text
M9 Quest RGB camera ──► M16 stereo probe ──► API-calibration + optical-sync gate
          │                                      │
          │                                      └─► same-pair stereo depth ─┐
          └────────► M10 RF-DETR detection ──────────────────────────────────┼─► revised M14
                                                                             │
M12 environment depth ──► occlusion, raycasts, coarse scene geometry ────────┘
```

**Capability and calibration first.** Milestone 16 measured a viable concurrent
left/right transport but found no factory distortion arrays or certified
optical sync relationship. The stereo calibration model is sourced from the
Meta Passthrough Camera API rather than a manual target procedure: intrinsics
from Camera2 `LENS_INTRINSIC_CALIBRATION` and per-camera extrinsics from
`LENS_POSE_ROTATION`/`LENS_POSE_TRANSLATION`. The next work is the bounded
software validation of that API-provided model — reprojection consistency and
rectified vertical disparity measured on live scenes — plus the optical timing
gate recorded in `docs/stereo-capability.md`. No production stereo-depth or
object-pose claim may precede that gate.

Environment Depth remains independently valuable, but it is no longer the
assumed object-ranging input for RF-DETR. Its image has its own time, pose, and
field of view, while a detection box is not an object mask.

Each milestone introduces one principal uncertainty. Model export and host C++
inference are deliberately *not* a milestone: that ground is already covered by
existing work, so the host pipeline in `tools/` serves as the reference oracle
that the on-device path is measured against.

| Milestone | New uncertainty | Observable result |
|---|---|---|
| 9 | camera API and adapter lifecycle | live/replayed RGB preview |
| 16 | concurrent stereo topology, timing, and calibration availability | pixel-free capability report and explicit debt |
| 12 | Meta depth acquisition and metric conversion | depth view and distance probe |
| 13 | time and coordinate registration | depth samples aligned over RGB |
| 10 | inference in the XR frame loop, and image-to-bearing projection | frame-correlated detections, world-space bearing rays |
| 11 | transport fault tolerance | streaming backend survives server faults |
| 14 | detection/depth fusion | world-locked metric 3D boxes |

A monocular detection yields a bearing and nothing else. Milestone 10 therefore
renders bearing rays and a 2D diagnostic preview, and makes no claim about
distance, extent, or orientation. Milestone 14 is the first milestone that draws
a 3D box, and by then it has measured range, depth-clustered extents, and a real
orientation convention.

---

# Milestone 9 — Quest Camera Capture

**Status:** Implemented with host tests and an Android debug build; Quest 3/3S
permission, visual, lifecycle, long-run, private-fixture, and replay
acceptance remain pending.

## Goal

Access one Quest 3 forward-facing RGB camera safely from the native
application, display a live preview, and create an opt-in recorded replay
fixture. This milestone proves only camera access, metadata, ownership, and
lifecycle behavior.

## Tasks

- Add runtime permission handling for Quest passthrough camera access.
- Define an engine-independent RGB camera-source interface.
- Implement Meta Camera2/Camera NDK behind an adapter.
- Implement recorded replay as a second adapter.
- Select adapters through a camera-source factory and explicit configuration.
- Capture one Quest RGB camera through the Meta adapter.
- Record image timestamps, intrinsics, lens pose, and selected stream format.
- Handle YUV plane strides and image lifetime correctly.
- Display the captured image on a native Vulkan diagnostic quad.
- Bound capture queues and drop old frames instead of blocking OpenXR.
- Stop and restore capture across Android pause/resume.
- Add a deterministic recorded-frame replay path.

## Deliverables

- `apps/09-quest-camera`.
- `libs/camera_source`.
- Meta Camera2 and replay camera-source adapters.
- A privacy-reviewed recorded RGB fixture and manifest.
- `docs/quest-camera.md`.

## Definition of Done

App 09 shows a live feed from a real Quest passthrough camera on a diagnostic
quad, logs correct capture metadata, survives lifecycle transitions, records
only after an explicit developer action, and replays one approved frame
deterministically through the same interface. Switching between Meta and
replay sources requires only factory configuration. No inference, networking,
or depth is required.

---

# Milestone 10 — RF-DETR Detection and Bearing

**Status:** Core implementation complete; headset acceptance measurements are
pending. App 10, all three detector adapters, Android ONNX Runtime packaging,
projection tests, the host service, one-command deployment, and Quest launch are
implemented. Approved-fixture agreement and 15-minute thermal evidence remain
blocked by the Milestone 9 fixture and an operator-in-headset run.

**Sequenced after Milestones 12 and 13.** The detector runs on Quest today, but
its output is not spatially useful until range exists.

## Goal

Run RF-DETR from the Quest — on the headset or on a streaming host, selected by
configuration — correlate every result to the frame that produced it, and
project each detection into a `LOCAL` bearing. This milestone proves inference
packaging, execution off the render thread, frame correlation, and the
unprojection and pose chain, not model export or host C++ inference, which are
already covered.

**Superseded on disk: app 10 now draws depth-fused 3D boxes.** This section is
kept for the reasoning, which still governs. A monocular detection determines a
direction and nothing more. An earlier revision placed a box at an operator-set
distance; because that box had to face the camera to have any shape at all, it
rendered as a billboard, and it was removed rather than refined. Bearing rays
replaced it and were then also rejected: they emanate from the viewer's own eye
position, so they read as a starburst rather than as a located object.

What app 10 draws today is a metric box fused from `XR_META_environment_depth`,
and only when depth supports a metric fit — otherwise nothing is drawn, so a
depth failure stays visible. Boxes whose far face came from a prior are tinted;
orientation comes from a RANSAC plane, or falls back to the viewing bearing
marked as a display convention. This absorbs the mechanism of Milestones 12 and
14 into app 10. **It does not discharge them:** neither the depth accuracy of
Milestone 12 nor the IoU, centre, extent, and jitter measurements of Milestone
14 have been taken on a headset.

## Tasks

- Define an engine-independent object-detector interface, factory, and health
  contract.
- Package a pinned ONNX Runtime build for `arm64-v8a` into an OpenXR/Vulkan APK.
- Implement on-device, streaming, and replay detector adapters behind one
  interface.
- Run inference on worker threads with bounded, newest-frame submission.
- Correlate results to the originating frame ID and expire stale results.
- Unproject 2D boxes into `LOCAL` bearing rays through the RGB intrinsics and
  camera pose, with inverse distortion documented.
- Render bearing rays in stereo over passthrough, and keep a 2D diagnostic
  preview. Claim no distance, extent, or orientation.
- Verify model identity at model load and negotiate it at service connect.
- Reproduce the host reference from both backends within stated tolerances.
- Measure latency, frame-delivery impact, and thermal behaviour per backend, and
  select a documented default.

## Deliverables

- `apps/10-rfdetr-detection`.
- `libs/object_detector`.
- `libs/detection_projection`.
- Android arm64 ONNX Runtime packaging.
- A minimal detection service wrapping the reference oracle.
- `docs/rfdetr-detection.md`.
- `tools/rfdetr_export` and `tools/rfdetr_inference` retained as the pinned
  export environment and host reference oracle.

## Definition of Done

App 10 detects real objects on Quest 3/3S from live and replayed camera frames,
with inference on device or streamed to a host by configuration alone. Every
result is tied to the frame that produced it, stale and mismatched results are
withheld, depth-fused metric boxes are rendered in stereo over passthrough with
nothing drawn when depth supplies no fit, both backends reproduce the host
reference within stated tolerances, and measured OpenXR frame phases are
unchanged while inference runs. Any dimension taken from a prior rather than
from depth is visibly marked as such.

---

# Milestone 11 — Live RF-DETR 2D Detection

**Status:** Planned in `specs/milestone-plans/milestone11-plan.md`; blocked by
Milestones 9 and 10. **Rescoped.** Milestone 10 introduces the streaming
detector adapter and a minimal service on a trusted local link, so this
milestone no longer adds an application or a detection capability. It hardens the transport that already
exists: explicit protocol versioning, negotiation failure, overload shedding,
disconnect and reconnect behaviour, malformed and duplicate frame handling, and
fault-injection coverage. `apps/11-rfdetr-live` is therefore dropped in favour
of extending `apps/10-rfdetr-detection`.

## Goal

Stream bounded-rate Quest camera frames to the real RF-DETR inference service,
correlate responses by frame ID, and draw returned 2D boxes on the camera
preview quad. This milestone proves live transport and temporal identity, not
metric 3D.

## Tasks

- Define an explicitly encoded, versioned, bounded protocol.
- Use framed reliable transport for image payloads and responses.
- Negotiate the RF-DETR model manifest.
- Send only the newest eligible RGB frame.
- Return boxes, class IDs, confidence, and inference timestamps.
- Reject malformed, duplicate, unknown, and expired frame IDs.
- Overlay boxes on the exact retained preview frame.
- Handle server absence, overload, disconnect, and reconnect.
- Measure capture-to-preview-result latency and queue occupancy.

## Deliverables

- `libs/perception_protocol`, extracted from the Milestone 10 streaming adapter.
- `tools/rfdetr_inference_server`, hardened from the Milestone 10 service.
- Host codec, framing, and fault-injection tests.
- Extensions to `apps/10-rfdetr-detection`; no new application.

## Definition of Done

The streaming backend of app 10 survives server absence, overload, disconnect,
reconnect, protocol-version mismatch, and malformed or duplicate responses
without restarting OpenXR. Queues remain bounded, stale results stay hidden, and
the frame loop runs at full rate throughout every fault.

---

# Milestone 16 — Stereo Capability Probe

**Status:** Implemented and measured on Quest 3. Verdict:
`PASS_WITH_DEBT`. P1A, P2, P3, and P4 passed. P1B is addressed by consuming
the intrinsics and lens-pose extrinsics that the Meta Passthrough Camera API
exposes through Camera2 and validating them in software; no manual target
calibration is planned. P3O still requires an optical synchronization
validation. See `specs/milestone-plans/milestone16-plan.md` and
`docs/stereo-capability.md`.

## Goal

Measure whether the left and right passthrough RGB cameras provide a viable
same-pair stereo foundation before committing Milestones 12–14 to an
environment-depth fusion design.

## Definition of Done

The pixel-free probe builds, deploys, captures more than 300 concurrent pairs,
writes a machine-readable report, and a host evaluator distinguishes hard
platform failures from recoverable calibration and validation debt.

---

# Milestone 12 — Environment Depth

**Status:** Partially implemented on disk, ahead of this plan. `libs/depth_source`
exists with a Meta `XR_META_environment_depth` adapter, metric conversion, and
host tests, and app 10 already starts it, acquires a depth image per frame, and
consumes it. What remains is this milestone's actual evidence: a standalone
depth application, the diagnostic visualization and probe, and measured error
against several physical surfaces. Nothing here has been validated on a
headset. Retain the milestone for occlusion, raycasts, and coarse scene
geometry; do not assume it is registered object depth for RF-DETR boxes.

## Goal

Acquire Meta environment depth independently of object detection, convert it
to metric distances, and validate it against measured physical surfaces.

## Tasks

- Request spatial-data permission.
- Define an engine-independent depth-source interface and factory.
- Implement Meta environment depth behind an adapter.
- Enable and capability-check `XR_META_environment_depth`.
- Create, start, stop, and destroy the provider and readable swapchain.
- Acquire one depth image at the valid point in each OpenXR frame.
- Preserve near/far, FOV, pose, dimensions, and display-time metadata.
- Convert depth texture values into metric distance.
- Add a depth diagnostic visualization and probe.
- Reject the unreliable near field and invalid values.
- Measure error for several static surfaces.

## Deliverables

- `apps/12-environment-depth`.
- `libs/depth_source`.
- Meta environment-depth adapter.
- Depth projection/math tests.
- `docs/environment-depth.md`.

## Definition of Done

App 12 visualizes live environment depth, reports plausible metric distances,
matches several measured surfaces within documented error, and survives three
clean lifecycle cycles without OpenXR errors. The application consumes only
the generic depth-source interface.

---

# Milestone 13 — RGB and Depth Alignment

**Status:** Plan requires revision after Milestone 16. Camera2-to-OpenXR time
correlation remains required for either stereo or environment-depth geometry.
The RGB-to-environment-depth path is no longer automatically the primary object
ranging design.

## Goal

Prove the spatial and temporal registration between Quest Camera2 RGB and Meta
environment depth before adding inference. Reproject depth samples into the
RGB preview and measure alignment error.

## Tasks

- Determine and validate the Camera2 sensor timestamp timebase.
- Correlate Camera2, monotonic, and OpenXR time domains.
- Locate the HMD and RGB camera pose at capture time.
- Match each RGB frame to a bounded retained depth snapshot.
- Compose RGB and depth sources through a sensor-rig factory.
- Transform depth pixels through depth view, `LOCAL`, and RGB camera spaces.
- Project samples with RGB intrinsics and distortion conventions.
- Display sparse depth reprojection over the retained RGB preview.
- Reject temporally distant or geometrically invalid pairs.
- Measure pixel error while static and during slow head motion.

## Deliverables

- `apps/13-rgb-depth-alignment`.
- `libs/sensor_rig`.
- Reusable timestamp-correlation and reprojection code.
- `docs/quest-camera-depth-calibration.md`.

## Definition of Done

Depth samples align with corresponding physical edges in the Quest RGB
preview within a documented pixel tolerance, including a controlled slow-head-
motion test. Failure to map timestamp domains reliably is reported as a
blocker rather than hidden by an approximation.

---

# Milestone 14 — RF-DETR Spatial Overlay

**Status:** Plan requires revision after the Milestone 16 calibration and
optical-sync gate. A working frame-correlated detection source already exists
in Milestone 10. If the follow-up gate passes, this milestone must consume
depth from the same stereo pair and isolate the object inside the detection;
box-only Environment Depth sampling is not the production design.

**The fusion mechanism already exists in app 10.** `libs/detection_fusion`
clusters depth samples inside a detection and produces a metric centre,
conservative extents, and a fusion confidence, and app 10 renders the result.
So this milestone no longer introduces 3D boxes — it is now the milestone that
**measures whether they are right**: 2D IoU, depth error, 3D centre error,
extent error, and jitter, against at least two real physical object classes.
None of those numbers exist yet. Until they do, the boxes app 10 draws are a
mechanism, not a validated result.

## Goal

Combine live RF-DETR detections with aligned environment depth to create
metric, world-space 3D annotations over passthrough.

## Tasks

- Retrieve the retained RGB/depth record for each RF-DETR frame ID.
- Select and robustly cluster depth samples inside each 2D detection.
- Compute a metric center, conservative extents, and fusion confidence.
- Transform results into OpenXR `LOCAL`.
- Render confidence- and age-coded 3D boxes.
- Hide invalid, low-confidence, stale, or uncorrelated results.
- Measure 2D IoU, depth error, 3D center error, extent error, and jitter.
- Validate at least two real physical object classes.

## Deliverables

- `apps/14-cv-spatial-overlay`.
- Reusable RGB/depth/detection fusion.
- Recorded replay and synthetic fault fixtures.
- `docs/milestone14-validation.md`.

## Definition of Done

Real RF-DETR detections become metric 3D overlays that remain world-locked
around static physical objects. Accuracy, jitter, latency, failure cases, and
limitations are measured quantitatively; mock detections alone cannot satisfy
completion.

---

# Milestone 15 — Performance and Production Quality

**Status:** Instrumentation, benchmark-build, host-quality, schema, capture,
and architecture foundations are implemented. Device baselines, measured
optimizations, media, and final acceptance remain blocked by outstanding
acceptance for Milestones 1–3 and 6–9 and implementation of Milestones 10–14.

## Goal

Improve the experiments with performance instrumentation, robustness, documentation, and production-quality engineering practices.

## Tasks

- Add CPU and GPU frame-time instrumentation.
- Track missed frames and stale perception data.
- Profile Camera2 capture, depth acquisition, readback, transport, RF-DETR
  inference, fusion, and rendering independently.
- Evaluate RF-DETR model size, input resolution, quantization, and C++ runtime
  backends against an explicit accuracy/latency budget.
- Benchmark an on-device RF-DETR path only after the host path is correct.
- Add temporal association and tracking without hiding raw detector output.
- Replace box-only depth sampling with segmentation-assisted fusion if the
  measured geometry requires it.
- Minimize allocations in the frame loop.
- Introduce RAII wrappers for OpenXR and Vulkan handles where useful.
- Add structured logging levels.
- Add CI checks for formatting and host-buildable unit tests.
- Add sanitizers for host-side libraries where applicable.
- Document Quest thermal and mobile GPU constraints.
- Add screenshots or short recordings for each completed application.
- Add an architecture diagram.

## Definition of Done

The repository demonstrates clean native architecture, repeatable builds, measured performance, and multiple independently understandable XR examples.

---

# Recommended Execution Order

1. Stabilize build and deployment.
2. Implement the OpenXR lifecycle from first principles.
3. Add Vulkan stereo rendering.
4. Master poses, spaces, and transforms.
5. Add controller interaction.
6. Add passthrough.
7. Implement stable object placement.
8. Add hand tracking.
9. Add anchors.
10. Capture and preview Quest RGB.
11. Probe concurrent stereo capability (Milestone 16, completed with debt).
12. Validate optical synchronization and the API-provided stereo calibration
    (Camera2 intrinsics and lens-pose extrinsics), persisting the validated
    model as the per-device artifact.
13. If that gate passes, implement and validate same-pair stereo depth; if it
    fails, retain honest bearing rays and move metric pose estimation off-device.
14. Establish Camera2-to-OpenXR time and pose correlation for world placement.
15. Run RF-DETR on one member of the exact retained stereo pair and isolate the
    detected object before metric extent estimation.
16. Retain Environment Depth for occlusion, raycasts, and independently useful
    scene geometry (Milestone 12), not as assumed box depth.
17. Rewrite and implement Milestones 13 and 14 against the validated branch.
18. Profile, harden, document, and consolidate the project.

---

# First Agent Assignment

The first coding-agent task should be:

> Inspect the existing repository and implement or repair the smallest repository-owned native OpenXR application that launches on Meta Quest 3. It must create an OpenXR instance and session, handle Android and OpenXR lifecycle events, log available extensions and session-state transitions, build through the existing scripts, install through ADB, and shut down cleanly. Preserve the current working setup and document every command required to reproduce the result. Do not add Unity or Unreal.

Expected output:

- a concise summary of the existing architecture;
- a list of files added or changed;
- a working build and deployment path;
- documentation of assumptions and unresolved platform issues;
- no unrelated refactoring.
