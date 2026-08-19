# RF-DETR detection in headset space

App 10 is an installable native Meta Quest application. Its default backend
runs RF-DETR Nano inside the APK with ONNX Runtime XNNPACK plus CPU fallback.
The same APK can instead use a trusted-link host service or pinned replay
detections; backend selection is app-private configuration written by the
deployment script.

## Deploy and run

Connect one authorized Quest 3/3S and run from the repository root:

```bash
adb devices
./scripts/build_deploy.sh --app 10-rfdetr-detection
```

That single command:

1. prepares the checksum-pinned ONNX Runtime Android AAR;
2. builds the arm64 APK;
3. installs it and grants both required camera permissions;
4. streams `rfdetr-nano.onnx`, its manifest, and `detector.conf` into the
   application's private `files/` directory with `run-as`;
5. verifies the installed model SHA-256; and
6. launches the immersive activity.

The default model location on the host is
`build/models/rfdetr/rfdetr-nano.onnx`. Override it with `--model PATH`.
Use `--build-only` when no headset is connected.

Inspect runtime state with:

```bash
adb logcat -s RFDetrDetection:V QuestCamera:V OpenXR:V '*:S'
```

The app starts with only the world-space detection geometry over passthrough;
the flat diagnostic is hidden. B toggles the head-locked 2D diagnostic.
Detection class, confidence, frame age, inference time, backend health,
measured range, and drop counts are drawn into that diagnostic.

World geometry is a metric 3D box fused from environment depth. A box is drawn
only when depth supported a metric fit; otherwise nothing is drawn. Boxes whose
far face came from a prior rather than from depth are tinted so the assumed
dimension stays legible.

## Backends

On-device is the default and needs no running host process:

```bash
./scripts/build_deploy.sh \
  --app 10-rfdetr-detection \
  --backend ondevice
```

For streaming, first build and start the loopback-only service:

```bash
onnxruntime_root="$(tools/rfdetr_inference/prepare_onnxruntime.sh)"
cmake -S tools/rfdetr_inference -B build/rfdetr-inference \
  -DQUESTLAB_ENABLE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT="$onnxruntime_root"
cmake --build build/rfdetr-inference --parallel
build/rfdetr-inference/rfdetr_service \
  --manifest tools/rfdetr_export/model-manifest.json \
  --model build/models/rfdetr/rfdetr-nano.onnx \
  --port 48110
```

In a second terminal, deploy with:

```bash
./scripts/build_deploy.sh \
  --app 10-rfdetr-detection \
  --backend streaming
```

The deploy script installs an `adb reverse` mapping for port 48110. The
versioned handshake exchanges the 64-character model identity and refuses a
mismatch before any pixels are sent. The service binds only to
`127.0.0.1`; it is intentionally a trusted developer-link implementation, not
a network-exposed production service.

Replay uses one pinned detection JSON for every submitted frame unless a
`replay_frame_id` is configured:

```bash
./scripts/build_deploy.sh \
  --app 10-rfdetr-detection \
  --backend replay \
  --replay-detections build/rfdetr-results/frame-001.json
```

## Runtime architecture

`libs/object_detector` exposes portable capabilities, configuration, result,
statistics, and health types. ONNX Runtime and socket handles remain private to
their adapters. Submission transfers capture ownership, is non-blocking, and
retains at most one pending frame. Rate-limited and replaced frames are counted.
Pause cancels an active ONNX run and joins all workers before session teardown.

The app's pipeline worker performs preview conversion, frame retention,
frame-ID matching, expiry, manifest checking, distortion inversion, LOCAL-space
projection, diagnostic annotation, and scene preparation. The OpenXR frame loop
only transfers the newest capture and consumes immutable prepared geometry. A
valid empty result clears the prior world geometry.

The retained record stores original-capture dimensions, pixels, intrinsics,
Camera2 calibration extrinsics, and the most recent `LOCAL` head pose associated
with submission.
Unknown, expired, dimension-mismatched, and manifest-mismatched results are not
drawn.

## Projection and current limitations

Camera pixels use top-left origin, +X right, and +Y down. The projection library
inverts Android Camera2's five-coefficient Brown-Conrady model (`k1`, `k2`,
`k3`, `p1`, `p2`) with at most 12 Newton iterations and a numerical Jacobian.
It rejects inversion residuals above 0.05 pixel. The optical ray remains in
Camera2's +Z-forward/+Y-down calibration basis while Android's
`LENS_POSE_ROTATION` is inverted from sensor-to-camera into camera-to-head.
`LENS_POSE_TRANSLATION` is used as the optical-center position in head/sensor
axes rather than incorrectly composed as the translation of that quaternion.
The resulting head-space ray is then transformed head-to-`LOCAL`.

Host tests round-trip a distorted pixel below 0.01 pixel, verify a known camera
origin in `LOCAL`, and regress the Quest forward-camera case: a Camera2 +Z
center ray must become OpenXR head -Z rather than appearing behind the user.
These tests do not replace headset world-locking measurements.

Milestone 13 has not yet established the Camera2-to-OpenXR timestamp mapping.
App 10 therefore records the most recent head pose when the capture is consumed,
marks that pose as substituted, and logs the limitation. Rapid head motion can
produce angular error.

Range comes from environment depth, not from an operator setting. Its accuracy
has not been measured on a headset, and the depth image has its own time, pose,
and field of view, so the fusion is not yet validated against physical
measurements. Neither limitation is silently presented as measured.

## Artifact pins and validation state

- ONNX Runtime Android: `1.21.0` official Maven AAR, SHA-256
  `8b675e9680b8cc02dca706a5e3b4e35cc8506de5bdf206fdae68081cbd804414`.
- RF-DETR Nano ONNX: SHA-256
  `1ac37bb3429380c7aee6b2c2778d20026db0ad18579cac119255d31f736dc760`.
- APK ABI: `arm64-v8a`; debug APK currently includes a 19,158,736-byte
  `libonnxruntime.so` and is about 22 MB before the private model is provisioned.

Validated locally on a connected Quest 3: APK install, private model checksum,
process launch, OpenXR runtime/session, Adreno Vulkan device, stereo swapchains,
passthrough, controller actions, and pause cancellation. The observed warm
frame windows stayed at about 72 frames/second with roughly 0.05 ms mean
renderer submission before live camera frames were admitted. Camera permission
is now granted by the deployment command.

Still requiring an operator wearing the headset and an approved fixture:
visible live detections, walking world-lock drift, backend-to-oracle numeric
agreement, a 15-minute thermal run, and a measured default-backend decision.
Those acceptance records must be added rather than inferred from a successful
build.

Meta's current native Camera2 path requires both `android.permission.CAMERA`
and `horizonos.permission.HEADSET_CAMERA`, Quest 3/3S, API 34, and a current
Horizon OS. See Meta's [native Camera2 documentation](https://developers.meta.com/horizon/llmstxt/documentation/native/android/pca-native-documentation.md).
ONNX Runtime's [XNNPACK guidance](https://onnxruntime.ai/docs/execution-providers/Xnnpack-ExecutionProvider.html)
recommends explicitly registering XNNPACK, disabling ORT intra-op spinning,
keeping the ORT intra-op pool at one, and assigning XNNPACK its own thread
count; app 10 follows that configuration.
