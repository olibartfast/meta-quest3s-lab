# Milestone 11 — Live RF-DETR 2D Detection

## Goal

Send live Quest RGB frames to the RF-DETR service, receive detections
correlated by frame ID, and draw 2D boxes on the exact retained camera-preview
frame.

This milestone proves live transport and temporal identity. It does not
convert detections into world-space geometry.

## Scope

Create:

- `apps/11-rfdetr-live`;
- `libs/perception_protocol`;
- `tools/rfdetr_inference_server`.

Reuse:

- Milestone 9 camera-source interface, factory, Meta adapter, and preview;
- Milestone 10 model manifest and C++ inference.

Exclude:

- environment depth;
- RGB/depth alignment;
- metric position or extent;
- world-space boxes;
- tracking and prediction.

## Atomic implementation sequence

### Sequence 0 — Create the live app shell

1. Create app 11 from app 09.
2. Rename package, target, label, library, and log tag.
3. Register Gradle, deploy-script, CI, and APK artifact entries.
4. Build and launch it.
5. Confirm the unchanged camera preview.

Gate: app 11 preserves the validated Milestone 9 behavior.

### Sequence 1 — Define the wire contract

1. Define literal magic bytes.
2. Define a protocol version.
3. Define message type, header length, and payload length.
4. Define message sequence and RGB `frameId`.
5. Define a bounded YUV frame message.
6. Define normalized `Detection2D`.
7. Define a detection-frame response.
8. Define hello/model-manifest negotiation.
9. Define status and ping/pong messages.
10. Set maximum image and detection counts.
11. Encode all numbers explicitly.
12. Reject NaN, infinity, inverted boxes, and invalid lengths.

Gate: golden byte vectors cover every message type.

### Sequence 2 — Implement reliable framing

1. Use a framed TCP byte stream.
2. Handle partial headers.
3. Handle partial payloads.
4. Handle multiple messages in one read.
5. Bound receive and send buffers.
6. Add connect, read, write, and shutdown deadlines.
7. Stop worker threads without waiting indefinitely.
8. Add capped reconnect backoff.

Gate: loopback tests survive fragmentation, coalescing, disconnect, and clean
shutdown.

### Sequence 3 — Add the inference server

1. Load the Milestone 10 model and manifest.
2. Reject a checksum mismatch.
3. Accept one development client.
4. Validate each RGB frame before allocation.
5. Run the existing C++ preprocessing and inference.
6. Undo letterboxing before serialization.
7. Echo the originating `frameId`.
8. Include receive, inference-start, and inference-end timestamps.
9. Return explicit overload and model errors.
10. Keep server queues bounded.

Gate: the server returns real detections for replayed protocol frames.

### Sequence 4 — Add the Quest client

1. Declare network permission.
2. Configure the server endpoint outside source code.
3. Negotiate protocol and model manifest.
4. Consume frames only through `IRgbCameraSource`.
5. Submit only the newest eligible frame.
6. Limit submission rate independently from capture rate.
7. Retain a bounded preview-frame history by `frameId`.
8. Receive validated detection frames.
9. Reject unknown, duplicate, future, or expired IDs.
10. Expose immutable correlated snapshots to the renderer.
11. Continue camera/OpenXR operation while disconnected.

Gate: live detections arrive with valid originating frame IDs and bounded
memory.

### Sequence 5 — Draw correlated 2D results

1. Retrieve the retained preview texture for the response `frameId`.
2. Draw boxes on that exact frame.
3. Preserve source aspect ratio and preview transforms.
4. Colour boxes by confidence.
5. Hide expired results.
6. Clear on a valid empty response.
7. Show connected, overload, stale, and disconnected diagnostics.
8. Log class IDs and confidence only on material changes.

Gate: moving the camera does not cause a box to be drawn over a newer,
unrelated preview frame.

### Sequence 6 — Failure and acceptance tests

1. Start the app before the server.
2. Start the server late.
3. Stop it during inference.
4. Restart it with the same model.
5. Attempt a mismatched model manifest.
6. Inject malformed lengths and boxes.
7. Saturate the server.
8. Run at several submission rates.
9. Measure total result age and queue occupancy.
10. Run for 15 minutes.
11. Build earlier apps and run host tests.

Gate: all failures are visible and safe, with no unbounded backlog.

## Definition of Done

- Quest camera frames reach the real C++ RF-DETR service;
- responses retain the exact source `frameId`;
- real 2D boxes appear on the corresponding retained preview frame;
- at least two physical object classes are detected live;
- malformed, mismatched, stale, and overloaded results are rejected;
- disconnect and reconnect do not restart OpenXR;
- latency and queue occupancy are measured;
- no depth or world-space claim is made.
