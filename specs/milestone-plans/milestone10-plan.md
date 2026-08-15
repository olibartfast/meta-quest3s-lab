# Milestone 10 — RF-DETR Detection and Bearing

## Goal

Run RF-DETR from the Quest — on the headset or on a streaming host, selected by
configuration rather than by a different application — correlate every result to
the frame that produced it, and project each detection into a bearing in OpenXR
`LOCAL`, rendered in stereo over passthrough.

This milestone is sequenced after Milestones 12 and 13. The detector already
runs on the headset, but its output does not become spatially useful until range
exists.

The milestone's uncertainty is deliberately not "can RF-DETR be exported and run
in C++". That is host work this repository already owns. The uncertainty is
whether a real detector can be driven *from Quest* — packaged into an
OpenXR/Vulkan APK or reached across a link, executed off the render thread,
correlated to the frame it actually saw, and sustained without destroying frame
delivery or thermal headroom.

Both backends sit behind one interface. The application never learns which one
is answering, because the properties the frame loop cares about — asynchronous
submission, bounded queues, frame-ID correlation, staleness, and honest health
reporting — are identical whether the latency comes from an on-device CPU or
from a network round trip. Getting that seam right is what makes the choice a
deployment decision instead of a rewrite.

Reference:

- [RF-DETR export guide](https://rfdetr.roboflow.com/latest/learn/export/)
- [ONNX Runtime XNNPACK execution provider](https://onnxruntime.ai/docs/execution-providers/Xnnpack-ExecutionProvider.html)
- [ONNX Runtime Android build](https://onnxruntime.ai/docs/build/android.html)
- [`olibartfast/rf-detr-cpp-inference`](https://github.com/olibartfast/rf-detr-cpp-inference)

## Preconditions

Milestone 10 is blocked by Milestone 9. It consumes the Milestone 9 camera
source, its replay adapter, and an approved fixture, and Milestone 9's
private-fixture and replay acceptance are still pending.

Sequences 0 through 7 may proceed against the existing replay path and synthetic
geometry. Sequence 8 needs an approved fixture, because agreement with the host
reference is only meaningful on the real camera's pixels, and the world-locking
checks in Sequences 5 and 6 need the live camera on a headset.

## What already exists, and what it is now for

The host pipeline built earlier in this milestone is kept, but it is no longer
the deliverable:

- `tools/rfdetr_export` remains the pinned export environment, model manifest,
  and reference-detection generator. Unchanged in role.
- `tools/rfdetr_inference` is demoted from milestone deliverable to **host
  reference oracle**. Its job is to produce the expected results that the
  on-device path must reproduce, and to stay the place where the model contract
  is debugged with a debugger and a fast iteration loop.

Nothing there is discarded. The offline tool becomes the thing that says whether
the device is right, which is a more useful role than being the destination.

## Why this milestone draws no 3D box

A monocular 2D box determines a **direction**, not a **distance**. Unprojecting
it through the RGB intrinsics yields a frustum, and every point along that
frustum is consistent with the same detection. Milestone 12 measures range; this
milestone does not have it, and no rendering technique substitutes for it.

An earlier revision of this plan tried to have it both ways: draw a wireframe box
at an operator-set distance, scaled to the frustum cross-section, and label the
range as assumed. That failed in practice. A cross-section has width and height
but no depth, so the box was a 4 cm plate; and to have any shape at all it had to
be rotated to face the camera, making it a billboard. A billboard looks identical
from every angle, so walking around it changes nothing and it reads exactly like
a 2D overlay pinned in space. It was removed, along with the range control and
every assumed field on `DetectionProjection`.

What remains is what is measured: bearing rays in `LOCAL`, rendered in stereo,
which track the physical object as the user moves. That is a real validation of
the unprojection and pose chain, and it is honest about the missing dimension.

None of this work is throwaway. Unprojection, the camera-pose chain, and the
`LOCAL` transforms are exactly what Milestone 14 consumes; it adds measured
range, depth-clustered extents, and a real orientation convention, and only then
does a box appear.

Two honest limitations to record rather than engineer around:

- This is the one place in the roadmap that needs **inverse** distortion, since
  it maps image points outward into rays. Milestone 13's depth reprojection runs
  the other way and needs only forward distortion. Document the inversion method
  and its residual error.
- Placing the frustum requires the head pose **at capture time**, which needs the
  Camera2-to-OpenXR timestamp mapping that Milestone 13 establishes. Until then,
  use the most recent pose, measure the resulting lag under head motion, and
  state it as a known limitation. Do not approximate silently — the size of that
  error is the argument for Milestone 13.

## Scope

Create:

- `apps/10-rfdetr-detection`;
- `libs/object_detector`, with the interface, factory, and adapters below;
- `libs/detection_projection`, holding unprojection, the camera-pose chain, and
  the `LOCAL` placement math, host-testable with synthetic geometry;
- Android arm64 ONNX Runtime packaging;
- a minimal streaming backend and the host service it talks to;
- `docs/rfdetr-detection.md`.

Reuse without redesign:

- Milestone 9's `IRgbCameraSource`, its factory, the Meta Camera2 and replay
  adapters, `LatestFrameQueue`, `ConvertYuv420ToRgba`, and the `intrinsics` and
  `cameraFromHead` already carried on every `RgbCapture`;
- passthrough composition from app 05;
- the existing `DebugLineShape::Box` and `Ray` shapes, the line-list pipeline,
  and the stereo per-eye transform in `libs/vulkan_renderer`;
- the app 09 preview quad and its frame-ID-keyed upload, retained as a
  toggleable diagnostic view;
- the pinned model manifest, class index space, and preprocessing contract
  already established in `tools/rfdetr_export/model-manifest.json`;
- `libs/perf_telemetry` for stage timing rather than a new timing path.

Exclude:

- transport hardening — protocol versioning, reconnect storms, overload
  shedding, and fault injection are Milestone 11, which now hardens this
  milestone's streaming adapter rather than introducing a separate application;
- environment depth and any measured range;
- metric extents, depth-clustered geometry, or fusion confidence, which require
  Milestones 12 through 14;
- estimating distance from object class or apparent size, which fabricates a
  measurement the sensor did not make;
- model fine-tuning, quantization, or architecture changes;
- temporal tracking or detection smoothing;
- GPU or NPU execution providers as an acceptance requirement;
- authentication, encryption, or exposure beyond a trusted local link.

## Detector architecture

Mirror the Milestone 9 and Milestone 12 boundary rather than inventing a new
shape:

```cpp
enum class ObjectDetectorKind {
    OnDeviceOnnxRuntime,
    Streaming,
    Replay,
};

class IObjectDetector {
public:
    virtual ~IObjectDetector() = default;

    virtual DetectorCapabilities GetCapabilities() const = 0;
    virtual bool Start(const DetectorConfig& config) = 0;
    virtual bool Submit(const RgbCapture& capture) = 0;
    virtual bool TryConsumeLatest(DetectionResult* result) = 0;
    virtual DetectorStats GetStats() const = 0;
    virtual void Stop() = 0;
};

std::unique_ptr<IObjectDetector> CreateObjectDetector(
    const DetectorConfig& config);
```

`OnDeviceOnnxRuntimeDetector` owns the session, the worker thread, and every
ONNX Runtime type. No `Ort::` handle escapes it. `StreamingDetector` owns the
socket, the send and receive workers, and every protocol type; no transport type
escapes it either. `ReplayDetector` returns pinned reference detections for a
given frame ID, which makes correlation, rendering, and rejection testable on
the host with no weights, no server, and no headset.

`Submit` is non-blocking and keeps only the newest eligible frame.
`TryConsumeLatest` returns a completed `DetectionResult` carrying the `frameId`
of the capture it was computed from, its own inference timestamps, and the model
manifest identity it was produced under.

### Why one interface covers both

The seam works because the application's requirements are already the
requirements a network imposes. Submission is asynchronous and lossy by design,
results arrive late and out of step with the render loop, every result must
carry the identity of the frame it saw, and the backend must be able to say it is
unhealthy without stopping the frame loop. An on-device detector needs all of
that because a mobile CPU is slow; a streaming detector needs all of it because
a link is slow and unreliable. The differences that remain are ones the app is
entitled to ignore.

Two contract details make the substitution honest rather than superficial:

- **Health is part of the interface.** `DetectorStats` carries a
  `DetectorHealth` state and a last-error string, mirroring `CameraSourceStats`.
  "Model file missing" and "server unreachable" are the same category of fact to
  the application: the detector cannot currently answer, and the user should be
  told.
- **Model identity is negotiated, not assumed.** Every result carries the
  manifest identity it was produced under. The on-device adapter verifies it
  against the loaded model at startup; the streaming adapter exchanges it with
  the service at connect and refuses a mismatch. A backend silently running a
  different model than the one the boxes are interpreted against is the failure
  this prevents.

Both backends are therefore comparable on the same fixture, in the same app,
against the same host reference — which is what lets the choice be made from
measured latency, accuracy, and thermal data rather than from preference.

## Atomic implementation sequence

### Sequence 0 — Create the app shell

1. Create app 10 from app 09, inheriting the camera source factory, the preview
   quad, and the permission flow.
2. Rename all application identifiers.
3. Register Gradle, `settings.gradle`, `build_deploy.sh`, CI, and APK artifact
   entries.
4. Build, deploy, and confirm the unmodified camera preview still works on the
   headset from both the Meta and replay sources.

Gate: app 10 is a clean, deployable camera-preview baseline before any detector
code exists.

### Sequence 1 — Package ONNX Runtime for Quest

1. Pin the ONNX Runtime Android artifact and record its version and checksum
   next to the existing host pin. Device and host versions must match, because
   Sequence 8 compares their numeric output.
2. Obtain the prebuilt Android package and extract `jni/arm64-v8a/
   libonnxruntime.so` and the C++ headers, following the same explicit,
   checksum-verified preparation script pattern already used on the host. Do not
   introduce a build-time download into CMake.
3. Package the shared library into the APK for `arm64-v8a` only.
4. Link it from the app's CMake target and confirm the APK loads it on device.
5. Log the runtime version and available execution providers at startup.
6. Record the APK size before and after. A detector that doubles the package is
   a documented cost, not a surprise.
7. Select the CPU execution provider with XNNPACK as the acceptance target.
   Treat any GPU or Qualcomm NPU provider as a later experiment with its own
   evidence, never as a prerequisite.

Gate: an OpenXR/Vulkan APK containing ONNX Runtime launches on Quest and logs
its runtime identity without loading a model.

### Sequence 2 — Define the detector boundary

1. Define `DetectorCapabilities`, `DetectorConfig`, `DetectionResult`, and
   `DetectorStats` as portable types with no `Ort::`, socket, Android, OpenXR, or
   Vulkan handles.
2. Carry `frameId`, source capture timestamp, inference start and end times, and
   model manifest identity on every result.
3. Define `DetectorHealth` and a last-error string on `DetectorStats`, mirroring
   `CameraSourceStats`, so an unavailable model and an unreachable service are
   reported through one path.
4. Define the backend selection surface on `DetectorConfig`: kind, on-device
   model path, service endpoint, submission rate cap, and result expiry age.
   Selecting a backend must require configuration only, exactly as switching
   between the Meta and replay camera sources does.
5. Implement `CreateObjectDetector`.
6. Implement `ReplayDetector` over the pinned reference detections.
7. Add host tests for the factory, backend selection, replay determinism,
   unknown frame IDs, manifest-identity mismatch, and health reporting.

Gate: the correlation, rejection, health, and selection logic is exercised on
the host with no weights, no server, and no headset.

### Sequence 3 — Run on-device inference off the render thread

1. Implement `OnDeviceOnnxRuntimeDetector` using the manifest contract already validated
   on the host: squash resize, `1/255` then ImageNet normalization, RGB NCHW,
   `dets` and `labels`, sparse COCO IDs, global top-k after sigmoid, no NMS.
2. Reuse `ConvertYuv420ToRgba`. The device must not acquire a third YUV
   conversion.
3. Validate ONNX input and output metadata against the manifest at startup and
   fail to a visible unsupported state on mismatch.
4. Run the session on a dedicated worker thread, never on the OpenXR frame loop.
5. Bound submission to the newest eligible frame and drop the rest, counting
   each drop cause separately.
6. Run one warm-up inference during startup, not on the first user-visible
   frame.
7. Verify with `perf_telemetry` that `PumpFrame` phase timings are unchanged
   from the Sequence 0 baseline while inference runs continuously.
8. Confirm clean shutdown: the worker must be cancellable and joinable without
   blocking session teardown or Android pause.

Gate: sustained on-device inference runs continuously without altering measured
OpenXR frame phases, and no inference work appears on the render thread.

### Sequence 4 — Correlate results to their originating frame

1. Retain the RGB frames and their poses and intrinsics that were submitted, in
   a bounded ring keyed by `frameId`.
2. Attach each returned result to the exact frame it was computed from, not to
   the newest camera frame.
3. Expire results older than a documented age and stop drawing them.
4. Reject results whose frame ID is unknown, already expired, or produced under
   a different manifest identity.
5. Confirm box coordinates are in original-capture pixels with the origin
   convention recorded in the manifest, before anything is unprojected.
6. Keep the app 09 preview quad as a toggleable 2D diagnostic and draw the boxes
   on it. Verifying the 2D result is correct is a prerequisite to trusting the
   3D placement built on it, and it is the fastest way to tell a bad detection
   apart from a bad projection.

Gate: every result is provably tied to the frame that produced it, a stale or
unmatched result is withheld rather than used late, and the 2D diagnostic shows
correct boxes before any 3D work begins. This gate is backend-agnostic and must
hold unchanged for every adapter added afterwards.

### Sequence 5 — Project detections into `LOCAL` bearings

1. Create `libs/detection_projection` with no OpenXR, Vulkan, or Android
   handles, so the geometry is host-testable.
2. Unproject the 2D box corners and centre through the RGB intrinsics into rays
   in camera space, inverting the documented distortion model. Record the
   inversion method and its residual error.
3. Transform the rays into head space using the camera calibration pose carried
   on the capture.
4. Transform into `LOCAL` using the head pose for that frame. Until Milestone 13
   supplies the Camera2-to-OpenXR timestamp mapping, use the most recent pose and
   record the substitution explicitly in the diagnostics.
5. Add host tests with synthetic geometry: a known pixel with a known camera pose
   must produce a known `LOCAL` ray, the corner bearings must be unit-length and
   distinct, and the round trip back through Milestone 13's forward projection
   must return the original pixel.
6. Carry directions only. `DetectionProjection` must not hold a centre position,
   an orientation, or an extent, because none of those are measured. The type is
   the enforcement point: if the record cannot express a distance, no renderer
   can invent one.
7. Reject detections whose unprojection is numerically invalid or whose rays fall
   outside the camera's valid field.

Gate: synthetic cases project exactly, and a real detection produces a `LOCAL`
ray that continues to point at the physical object as the user walks sideways.

### Sequence 6 — Render what is measured

1. Draw the four corner bearings in `LOCAL` from the camera position, using the
   existing line-list pipeline, to a fixed display length. The length is a
   legibility choice and must be documented as such, not as a range.
2. Draw no box. A box requires a distance, an extent, and an orientation, and
   this milestone measures none of them. An earlier revision placed one at an
   operator-set distance; because it had to face the camera to have any shape,
   it rendered as a billboard — visually identical from every angle, which is a
   2D overlay pinned in space. Do not reintroduce it in any form, including a
   thin plate, a fixed-size cube, or a size estimated from object class.
3. Composite over passthrough using the app 05 underlay path.
4. Colour by detector confidence and fade by result age.
5. Show class label and confidence per detection, using the sparse COCO index
   space.
6. Keep the 2D diagnostic preview toggleable; it remains the fastest way to tell
   a bad detection from a bad projection.
7. Hide expired, unmatched, and manifest-mismatched results, and clear on a valid
   empty detection frame.
8. Display detection age, inference time, backend health, and the
   submitted-versus-dropped counts, and state plainly on screen that range is
   unmeasured.
9. Order draws by confidence, since no range exists to sort by and the renderer
   has no depth attachment.
10. Keep unprojection and scene construction off the render loop; the frame loop
    consumes prepared draws only.

Gate: bearings render in stereo over passthrough and track the physical object
as the user moves, and nothing on screen implies a distance, extent, or
orientation that was not measured.

### Sequence 7 — Add the streaming backend

1. Implement `StreamingDetector` behind the same interface, with send and
   receive on their own workers and no transport type escaping the adapter.
2. Wrap `tools/rfdetr_inference` as the service. The reference oracle already
   owns the validated model contract, so the service should call it rather than
   grow a second inference implementation.
3. Exchange the model manifest identity at connect and refuse a mismatch, so a
   service running a different model cannot silently answer.
4. Encode frame ID, capture timestamp, and image payload explicitly and
   versioned. Send only the newest eligible frame; never queue a backlog.
5. Apply the same expiry and rejection rules the on-device path uses. A late
   arrival is discarded by the Sequence 4 logic, not by transport-specific code.
6. Report connection state through `DetectorHealth`, and keep the frame loop
   running at full rate while disconnected, showing camera preview with no
   boxes.
7. Confirm switching backends requires only configuration: the same APK, the
   same rendering path, no conditional logic in the application layer.
8. Restrict the service to a trusted local link. Protocol hardening, reconnect
   behaviour, overload shedding, and fault injection are Milestone 11.

Gate: the same app, unchanged, draws correlated boxes from either backend, and
losing the service degrades to preview-without-boxes rather than to a stalled or
crashed frame loop.

### Sequence 8 — Prove both backends agree with the host

1. Run the approved fixture through the replay camera source on device, once per
   backend.
2. Run the same fixture through `tools/rfdetr_inference` on the host.
3. Compare class labels, confidences, and box geometry using the tolerances
   already fixed for the host comparison.
4. Expect the streaming backend to match the oracle closely, since it calls the
   same code, and treat any divergence there as a protocol or encoding defect.
5. Expect arm64 XNNPACK and x64 CPU results to differ slightly. Determine the
   real divergence, record it, and state the on-device tolerance separately
   rather than reusing the host-to-host number by assumption.
6. Treat a divergence larger than measurement noise as a contract defect —
   preprocessing, layout, or index space — and not as a tolerance to widen.
7. Save detections and an on-device screenshot per backend for the record.

Gate: both backends reproduce the host reference on identical input, each within
a stated and separately justified tolerance.

### Sequence 9 — Frame delivery, thermal, lifecycle, and the default

Measure both backends under the same scenario, and label every record with the
backend that produced it.

1. Measure inference latency as count, mean, p50, p95, p99, and maximum. For the
   on-device backend record the execution provider and thread count; for the
   streaming backend record the link and separate transport time from service
   inference time.
2. Measure end-to-end capture-to-drawn-box age per backend. This is the number
   that decides which backend is usable, and it is the only one that includes
   everything.
3. Measure the effect on frame delivery at the requested refresh rate, reporting
   stale frames and `App` GPU time with each backend and with detection off.
4. Choose and record a submission rate per backend that leaves frame delivery
   intact. The detector serves the frame loop; the frame loop does not wait for
   it.
5. Run each backend for 15 minutes and record thermal state, frequency or
   refresh-rate changes, and any degradation trend. Expect the on-device backend
   to be the thermally interesting one, and record the comparison rather than
   assuming it.
6. Test permission denial, pause/resume during inference, missing model file,
   corrupt model file, manifest mismatch, and service absence at startup and
   mid-run.
7. Verify bounded queues and no growth in memory high-water mark across each run.
8. Measure world-locking quality: with the placement distance dialled onto a
   static physical object, record the angular drift of the rendered geometry
   against that object while standing still, while walking sideways, and during
   slow head rotation.
9. Measure and record the pose-substitution error from Sequence 5 step 4
   separately, since it is the component that Milestone 13 is expected to remove.
10. Select and record a documented default backend, with the measured reasoning.
    The alternative stays selectable; this milestone produces a recommendation
    supported by data, not a deletion.
11. Run all host tests and build every earlier app.

Gate: frame delivery, latency, thermal behavior, world-locking drift, and
failure handling are measured per backend, and the default is justified by that
data.

## Model artifact handling

The exported model stays out of Git, consistent with the existing rule. Push it
to app-private storage with the same explicit developer action used for camera
fixtures, and have the app fail to a clear, visible state when it is absent
rather than crashing or silently rendering nothing. Record the expected on-device
path and the checksum the app verifies at load.

## Definition of Done

- app 10 launches on Quest 3/3S and renders real RF-DETR detections as stereo
  bearing rays in OpenXR `LOCAL` over passthrough, with inference running on the
  headset, and again with it running on a streaming host, selected by
  configuration alone;
- the rendered bearings track the physical object as the user moves, with
  measured angular drift while static, walking, and rotating;
- no distance, extent, or orientation is rendered or implied; the type system
  enforces this, since `DetectionProjection` carries directions only;
- ONNX Runtime is packaged for `arm64-v8a` from a pinned, checksum-verified
  artifact with no build-time download;
- every ONNX Runtime type stays inside `OnDeviceOnnxRuntimeDetector`, every
  transport type stays inside `StreamingDetector`, and the app depends only on
  `IObjectDetector`;
- inference runs on worker threads with bounded, newest-frame submission, and
  measured `PumpFrame` phases are unchanged from the pre-detector baseline for
  both backends;
- each drawn detection is tied to the frame it was computed from, and stale,
  unknown, or manifest-mismatched results are withheld regardless of backend;
- unprojection, the camera-pose chain, and `LOCAL` placement live in
  `libs/detection_projection` and pass synthetic-geometry host tests, including
  a round trip against the forward projection Milestone 13 will use;
- the distortion inversion method and its residual error are documented, as is
  the capture-time pose substitution and its measured error;
- model manifest identity is verified at model load and negotiated at service
  connect, and a mismatch is refused;
- both backends reproduce the host reference on the same approved fixture, each
  within a separately stated tolerance;
- inference latency, capture-to-box age, frame delivery, and a 15-minute thermal
  run are measured per backend, and a default is chosen from that data;
- losing the service degrades to passthrough without detections at full frame
  rate, and a missing or corrupt model fails visibly;
- the replay detector allows the correlation, projection, and rendering paths to
  be tested on the host with no weights, no server, and no headset;
- environment depth, measured range, and metric extents are not required, and
  are not claimed.
