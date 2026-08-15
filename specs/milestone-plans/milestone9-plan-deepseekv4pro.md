# Milestone 9 — Computer-Vision Overlay

## Goal

Connect a real-time C++ perception pipeline running on a separate machine to the Quest OpenXR application over UDP. Render timestamped detection data (3D bounding boxes, labels, confidence) as stable spatial overlays on top of passthrough. Measure end-to-end latency and document the coordinate calibration between the external perception system and the XR `LOCAL` reference space.

## Scope

Create:

- `libs/perception_client/` — UDP receiver, binary protocol decoder, thread-safe frame buffer
- `apps/09-cv-overlay/` — passthrough app rendering detections as spatial overlays
- `mock_perception_server/` — standalone C++ program sending synthetic detections
- `docs/perception-coordinate-calibration.md` — calibration transform documentation

Exclude: on-device camera access (Camera2, passthrough RGB), GPU inference, on-device CV, object tracking across frames, sensor fusion between Quest tracking and external perception, and multi-source perception (single server only).

## Repository baseline

Milestone 9 builds on the completed repository-owned stack:

- `xr_core` owns the instance, session, events, reference spaces, and the `PumpFrame` loop.
- `xr_meta_passthrough` supplies the passthrough underlay and lifecycle.
- `xr_controller_actions` supplies controller poses, triggers, and haptics.
- `vulkan_renderer` renders `DebugLineDraw` shapes (Box, Ray, Axes).
- Milestones 6–8 demonstrate controller-driven placement, hand tracking, and anchor persistence — all using the same app scaffold.
- `XrEventFanout` exists in `xr_core` (used by M8 to fan events to passthrough + anchors).

## Architecture

```
mock_perception_server/          libs/perception_client/         apps/09-cv-overlay/
├── CMakeLists.txt               ├── CMakeLists.txt              ├── CMakeLists.txt
├── README.md                    ├── include/perception_client/  ├── build.gradle
└── src/                         │   ├── perception_client.h     ├── README.md
    └── main.cpp                 │   └── perception_protocol.h   └── src/main/
                                 └── src/                            ├── AndroidManifest.xml
                                     ├── perception_client.cpp       └── cpp/
                                     └── perception_protocol.cpp         └── main.cpp

mock_perception_server links libs/perception_client for protocol types (host-only).
apps/09-cv-overlay links libs/perception_client + the existing XR stack (Android).
```

### Component relationship (on-device)

```
main.cpp
  ├── XrInstanceContext       (passthrough extension; no special CV extensions)
  ├── VulkanSessionBinding
  ├── XrSessionContext
  │     └── PumpFrame(&renderer, &scene, &passthrough)
  ├── MetaPassthroughFB       (camera underlay, XrEventObserver)
  ├── PerceptionClient        (new, from libs/perception_client)
  │     ├── Start(port) → spawns receive thread → UDP recvfrom loop
  │     ├── Poll: HasNewFrame() / GetLatestFrame() (mutex-guarded)
  │     └── Shutdown: join thread, close socket
  ├── CvOverlayScene          (implements XrFrameUpdater + VulkanSceneProvider)
  │     ├── UpdateFrame(): polls perceptionClient, tracks staleness
  │     ├── BuildScene(): applies calibration, emits DebugLineDraw per detection
  │     └── Renders status HUD dot, LOCAL origin marker
  ├── VulkanSwapchain         (transparent-clear, same as M5–M8)
  ├── VulkanPipeline
  └── Render loop (same M6–M8 pattern)
        ├── ALooper_pollOnce → Android events
        ├── xrSession.PollEvents(&passthrough)   // no fan-out needed
        ├── passthrough.SetActive(resumed && running)
        ├── xrSession.PumpFrame(&renderer, &scene, &passthrough)
        ├── cadenceLogger.Record(dt)
        └── layers: [passthrough underlay, projection layer]
```

### Layer stack (unchanged from M5)

```
┌─────────────────────────────────────┐
│  Vulkan projection layer            │  ← detection boxes + axes + HUD
│  flags: BLEND_TEXTURE_SOURCE_ALPHA   │
├─────────────────────────────────────┤
│  Passthrough reconstruction layer   │  ← camera feed
│  space: XR_NULL_HANDLE              │
└─────────────────────────────────────┘
   XR_ENVIRONMENT_BLEND_MODE_OPAQUE
```

## Perception Protocol

### Wire format

All fields little-endian. Fixed-size structs, no variable-length data. One complete frame per UDP datagram.

```
┌─────────────────────────────────────────────┐
│  Header: 16 bytes                           │
│  ┌───────────┬──────────┬────────┬────────┐│
│  │ magic: u32│ vers: u16│ cnt: u16│ts: u64 ││
│  │ 0x514C4250 │   1      │  N     │  ns    ││
│  └───────────┴──────────┴────────┴────────┘│
├─────────────────────────────────────────────┤
│  Detection payload: N × 48 bytes            │
│  ┌─────────────────────────────────────────┐│
│  │ id: u32 (4)                             ││
│  │ pos: 3 × f32 (12)    [x, y, z] meters  ││
│  │ ext: 3 × f32 (12)    [half-extents]     ││
│  │ confidence: f32 (4)   [0,1]            ││
│  │ label: char[16] (16)                    ││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘

Total: 16 + N×48 bytes
Ethernet-safe (≤1400): N ≤ 28
```

### C++ types (shared header, `libs/perception_client/`)

```cpp
// include/perception_client/perception_protocol.h
namespace questlab::perception {

constexpr std::uint32_t kProtocolMagic   = 0x514C4250;  // "QPLB"
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kMaxLabelLength    = 16;

struct PerceptionDetection {
    std::uint32_t id = 0;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float extentX   = 0.05F;
    float extentY   = 0.05F;
    float extentZ   = 0.05F;
    float confidence = 0.0F;
    char  label[kMaxLabelLength] = {};
};

struct PerceptionFrameHeader {
    std::uint32_t magic          = kProtocolMagic;
    std::uint16_t version        = kProtocolVersion;
    std::uint16_t detectionCount = 0;
    std::uint64_t timestampNs    = 0;
};

constexpr std::size_t kMaxDetectionsPerDatagram =
    (1400 - sizeof(PerceptionFrameHeader)) / sizeof(PerceptionDetection);

// Returns detection count on success, -1 on invalid header/size.
int DecodeFrame(
    const std::uint8_t* data,
    std::size_t         size,
    PerceptionFrameHeader*      header,
    PerceptionDetection*        detections,
    std::size_t                 maxDetections);

}  // namespace questlab::perception
```

`PerceptionDetection` uses flat `float` fields rather than arrays to simplify construction in the mock server and avoid padding surprises. `DecodeFrame` validates magic, version, and size; on failure it returns -1 and leaves outputs untouched.

## Reusable Library: `libs/perception_client/`

### `PerceptionClient` class

```cpp
// include/perception_client/perception_client.h
namespace questlab {

class PerceptionClient {
public:
    PerceptionClient() = default;
    ~PerceptionClient();

    PerceptionClient(const PerceptionClient&) = delete;
    PerceptionClient& operator=(const PerceptionClient&) = delete;

    bool Start(std::uint16_t port);
    void Stop();

    bool HasNewFrame() const { return hasNewFrame_.load(); }
    const std::vector<perception::PerceptionDetection>& GetLatestFrame();

    double MsSinceLastPacket() const;

private:
    void ReceiveLoop();

    int socketFd_ = -1;
    std::thread receiveThread_;

    std::mutex frameMutex_;
    std::vector<perception::PerceptionDetection> latestFrame_;
    std::atomic<bool> hasNewFrame_{false};

    std::chrono::steady_clock::time_point lastReceiveTime_{};
    std::uint64_t frameCount_ = 0;
};

}  // namespace questlab
```

### Thread model

- **Receive thread** (`ReceiveLoop`): blocks on `recvfrom()`. On arrival: decodes header, copies detections into a local vector, locks `frameMutex_`, swaps with `latestFrame_`, sets `hasNewFrame_` = true, records `lastReceiveTime_`, unlocks. Loops.
- **Render thread** (main): `BuildScene()` calls `HasNewFrame()` → if true, calls `GetLatestFrame()` (copies under lock), renders. Lock contention is sub-microsecond.

### Build

```cmake
# libs/perception_client/CMakeLists.txt
add_library(perception_client STATIC
    src/perception_client.cpp
    src/perception_protocol.cpp
)
target_include_directories(perception_client PUBLIC include)
target_compile_features(perception_client PUBLIC cxx_std_17)
if(NOT ANDROID)
    target_link_libraries(perception_client PUBLIC pthread)
endif()
```

Dual-target (host + Android). No OpenXR, Vulkan, or Android NDK dependency beyond `std::thread` (provided by the NDK on Android, by pthread on host).

## Mock Perception Server

### `mock_perception_server/`

Standalone C++17 program for Linux. Links `perception_client` for the protocol types. No Quest/OpenXR/Android dependency.

```
Usage: mock_perception_server --port PORT --count N --rate HZ
                               [--pattern circle|grid|random]
                               [--dest IP]

Options:
  --port     UDP destination port          (default: 9876)
  --count    Detections per frame          (default: 8, max: 28)
  --rate     Frames per second             (default: 30)
  --pattern  circle | grid | random        (default: circle)
  --dest     Destination IPv4 address     (default: 127.0.0.1)
```

### Patterns

| Pattern  | Behaviour |
|----------|-----------|
| `circle` | N detections orbit a 2.0m radius circle at z ≈ -2.0m in the XZ plane. Detection Y oscillates ±0.3m. Continuous slow rotation. |
| `grid`   | Static √N × √N grid on a vertical plane at z = -2.0m, centered on origin, 0.5m spacing. |
| `random` | Random positions within a [-3,3] × [-1,2] × [-4,-0.5] box, regenerated per frame. Confidence random [0.2, 1.0]. |

Labels: `"obj_0"`, `"obj_1"`, ... Padding to 16 bytes. Confidence fixed at 0.85 for `circle`/`grid`.

### Dev workflow

```bash
# Build mock server (host)
cmake -B build/mock_server mock_perception_server
cmake --build build/mock_server

# Run on dev machine
./build/mock_server/mock_perception_server --port 9876 --count 5 --pattern circle

# Forward to Quest via USB
adb reverse udp:9876 udp:9876
```

## App: `apps/09-cv-overlay/`

### Scene class (`CvOverlayScene`)

The scene implements both `XrFrameUpdater` and `VulkanSceneProvider`, following the pattern established by M6 and M8. It owns no controllers — this is a passive viewer.

```cpp
namespace {

constexpr float kDetectionFadeStartMs = 1000.0F;
constexpr float kDetectionFadeDurationMs = 4000.0F;

class CvOverlayScene final :
    public questlab::XrFrameUpdater,
    public questlab::VulkanSceneProvider {
public:
    explicit CvOverlayScene(questlab::PerceptionClient* client);

    bool UpdateFrame(
        const questlab::XrFrameUpdateInfo& frame) override;

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override;

private:
    questlab::PerceptionClient* client_;
    std::vector<questlab::perception::PerceptionDetection> detections_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    bool neverReceived_ = true;
};
```

### `UpdateFrame()` logic

Polls `client_->HasNewFrame()`. If true, copies the latest frame. Tracks `lastUpdate_` for staleness detection.

```cpp
bool CvOverlayScene::UpdateFrame(const questlab::XrFrameUpdateInfo& frame) {
    if (client_->HasNewFrame()) {
        detections_ = client_->GetLatestFrame();
        lastUpdate_ = std::chrono::steady_clock::now();
        neverReceived_ = false;
    }
    return true;
}
```

### `BuildScene()` rendering

For each detection, applies the calibration transform, builds a model matrix, and emits a `Box` + small `Axes`:

```cpp
bool CvOverlayScene::BuildScene(
    const questlab::XrFrameRenderInfo& frame,
    std::vector<questlab::DebugLineDraw>* draws) {
    if (draws == nullptr) return false;

    // LOCAL origin reference
    draws->push_back({
        questlab::DebugLineShape::Axes,
        questlab::math::Multiply(
            questlab::math::IdentityMatrix(),
            ScaleMatrix(0.25F, 0.25F, 0.25F)),
    });

    auto now = std::chrono::steady_clock::now();
    double ageMs = std::chrono::duration<double, std::milli>(
        now - lastUpdate_).count();

    for (const auto& det : detections_) {
        questlab::math::Pose detPose{questlab::math::IdentityQuat(),
            {det.positionX, det.positionY, det.positionZ}};
        questlab::math::Mat4 model = questlab::math::Multiply(
            questlab::math::PoseMatrix(detPose),
            ScaleMatrix(det.extentX, det.extentY, det.extentZ));

        std::array<float, 4> color;
        double fade = 1.0;
        if (ageMs > kDetectionFadeStartMs) {
            fade = std::max(0.0,
                1.0 - (ageMs - kDetectionFadeStartMs) / kDetectionFadeDurationMs);
        }
        if (det.confidence >= 0.7F)
            color = {0.1F * static_cast<float>(fade),
                     1.0F * static_cast<float>(fade),
                     0.2F * static_cast<float>(fade), 1.0F};
        else if (det.confidence >= 0.4F)
            color = {1.0F * static_cast<float>(fade),
                     0.85F * static_cast<float>(fade),
                     0.0F * static_cast<float>(fade), 1.0F};
        else
            color = {1.0F * static_cast<float>(fade),
                     0.15F * static_cast<float>(fade),
                     0.15F * static_cast<float>(fade), 1.0F};

        draws->push_back({questlab::DebugLineShape::Box, model, color});
        draws->push_back({questlab::DebugLineShape::Axes,
            questlab::math::Multiply(model, ScaleMatrix(0.3F, 0.3F, 0.3F)),
            color});
    }

    // Status HUD dot: head-locked, 0.5m forward, below eye line
    if (HasValidPose(frame.headInLocal.locationFlags)) {
        questlab::math::Pose headPose =
            questlab::math::FromXr(frame.headInLocal.pose);
        questlab::math::Pose hud{
            questlab::math::IdentityQuat(),
            questlab::math::Add(headPose.position,
                questlab::math::TransformDirection(
                    headPose, {0.0F, -0.25F, -0.5F}))};
        std::array<float, 4> statusColor;
        if (neverReceived_)
            statusColor = {1.0F, 0.15F, 0.15F, 1.0F};
        else if (ageMs < kDetectionFadeStartMs)
            statusColor = {0.1F, 1.0F, 0.2F, 1.0F};
        else
            statusColor = {1.0F, 0.85F, 0.0F, 1.0F};
        draws->push_back({questlab::DebugLineShape::Box,
            questlab::math::Multiply(
                questlab::math::PoseMatrix(hud),
                ScaleMatrix(0.03F, 0.03F, 0.03F)),
            statusColor});
    }

    return true;
}
```

### Calibration transform

A hardcoded identity transform. Perception coordinates are assumed to be in meters with the same axis convention as LOCAL (`+X` right, `+Y` up, `-Z` forward). The mock server generates coordinates in this frame. A future iteration can make `calibration_` configurable.

### App `main.cpp` scaffold (matching M8 pattern)

```cpp
void android_main(android_app* app) {
    questlab::SetLogTag("CvOverlay");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    questlab::LogInfo("Computer-vision overlay demo starting");
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    questlab::PerceptionClient perceptionClient;
    CvOverlayScene scene(&perceptionClient);
    questlab::VulkanStereoRenderer renderer;
    FrameCadenceLogger cadenceLogger;

    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "CV Overlay", 1,
        {XR_FB_PASSTHROUGH_EXTENSION_NAME}};
    const questlab::VulkanRendererOptions rendererOptions{true};

    if (!xrInstance.Initialize(
            app->activity->vm, app->activity->clazz, instanceOptions) ||
        !vulkanBinding.Initialize(xrInstance, bindingOptions) ||
        !xrSession.Initialize(
            xrInstance.Instance(), xrInstance.SystemId(),
            vulkanBinding.GraphicsBinding()) ||
        xrSession.BlendMode() != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ||
        !passthrough.Initialize(
            xrInstance.Instance(), xrInstance.SystemId(),
            xrSession.Session()) ||
        !perceptionClient.Start(9876) ||
        !renderer.Initialize(
            xrInstance.Instance(), xrSession,
            vulkanBinding.DeviceContext(), &scene, rendererOptions)) {
        questlab::LogError("CV overlay initialization failed");
        ANativeActivity_finish(app->activity);
    } else {
        while (!androidState.destroyRequested && !app->destroyRequested &&
               !xrSession.ShouldExit()) {
            // ... Android event polling (same as M8 lines 441–463) ...
            if (!xrSession.PollEvents(&passthrough) ||
                !passthrough.SetActive(
                    androidState.resumed && xrSession.IsRunning())) {
                break;
            }
            const bool frameRunning = xrSession.IsRunning();
            const auto frameStart = std::chrono::steady_clock::now();
            if (!xrSession.PumpFrame(&renderer, &scene, &passthrough)) {
                break;
            }
            if (frameRunning) {
                cadenceLogger.Record(
                    std::chrono::steady_clock::now() - frameStart);
            }
        }
    }

    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    perceptionClient.Stop();
    passthrough.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("CV overlay demo stopped cleanly");
}
```

### Init flow differences from M8

| Aspect | M8 (Spatial Anchors) | M9 (CV Overlay) |
|--------|---------------------|-----------------|
| Extensions | `XR_FB_spatial_entity`, `XR_META_spatial_entity_persistence`, `XR_FB_spatial_entity_query` | `XR_FB_passthrough` only |
| Event fan-out | `XrEventFanout` → passthrough + anchors | None; `PollEvents(&passthrough)` directly |
| Scene owns | `XrControllerActions` + `MetaSpatialAnchorManager*` | `PerceptionClient*` |
| Controller input | Trigger → create anchor; Primary → erase | None (passive viewer) |
| Frame updater | Controller sync + anchor-locate loop | Perception-client poll + staleness tracking |
| Directory | `internalDataPath` for UUID file | Not needed |

## Build system

### `libs/perception_client/CMakeLists.txt`

As shown above. Dual-target, links `pthread` on host only.

### `apps/09-cv-overlay/CMakeLists.txt`

Shared module `cv_overlay`. Links: `xr_core`, `vulkan_renderer`, `xr_math`, `xr_meta_passthrough`, `perception_client`, `android_native_app_glue`, `android`, `log`, `vulkan`, `OpenXR::openxr_loader`.

### `apps/09-cv-overlay/build.gradle`

Package `com.olibartfast.questlab.cvoverlay`. Library name `cv_overlay`. Label `CV Overlay`.

### `AndroidManifest.xml`

```xml
<uses-feature android:name="com.oculus.feature.PASSTHROUGH" android:required="true" />
<uses-permission android:name="android.permission.INTERNET" />
```

`INTERNET` permission is required for the UDP socket. No additional Oculus permissions.

### Repository integration

Add app 09 to:

- `settings.gradle`: `include(":apps:09-cv-overlay")`
- `scripts/build_deploy.sh`: `--app 09-cv-overlay` option
- `.github/workflows/android-ci.yml`: APK build + artifact upload

## Documentation: `docs/perception-coordinate-calibration.md`

Covers:

1. **Why calibration matters**: the external perception system (camera + CV pipeline) has its own coordinate frame. Without a transform, detections render in the wrong position relative to the user.
2. **The rigid transform**: 6-DOF `Pose` (rotation + translation) mapping perception-space → LOCAL-space. For M9 this is identity (mock server emits LOCAL-space coordinates).
3. **Estimating the transform**: collect ≥3 non-collinear point pairs visible to both the camera and the Quest (touched with a controller). Solve via Arun's method or centroid alignment + SVD.
4. **Frame conventions**: the Quest's LOCAL space is `+X` right, `+Y` up, `-Z` forward (OpenXR standard). The perception pipeline may use a different convention; document the conversion before applying the calibration transform.
5. **Static vs dynamic calibration**: calibration is valid only while the camera and Quest are stationary relative to each other. Moving either requires re-calibration.
6. **Common pitfalls**: quaternion `w` ordering, coordinate handedness mismatches, the Quest's LOCAL origin drift across sessions.

## Definition of Done

1. `apps/09-cv-overlay` builds, installs, and launches on Quest 3.
2. `mock_perception_server` builds and runs on the Linux dev machine.
3. Camera passthrough is visible as the background.
4. With the mock server running and reachable (via `adb reverse udp:9876:9876` or Wi-Fi), wireframe boxes appear at the server-specified positions (e.g., 5 boxes orbiting at ~2m distance).
5. Box colours reflect confidence: green (≥0.7), yellow (0.4–0.7), red (<0.4).
6. Boxes animate in real time as the server updates positions (circle pattern rotation).
7. When the server stops, boxes fade to grey over 1 second and disappear after 5 seconds. The HUD dot turns yellow then red.
8. Detection labels are logged to logcat when the first frame in a detection series arrives.
9. Frame cadence and perception receive statistics are logged once per second via `adb logcat -s CvOverlay`.
10. A cyan LOCAL-origin marker (RGB axes) is visible at `(0,0,0)` for spatial reference.
11. A small head-locked status dot (green/yellow/red) indicates connection health at ~0.5m.
12. Three clean launch/quit cycles without crashes.
13. No OpenXR or Vulkan validation errors.
14. Apps 01–08 and the legacy `XrPassthrough` app continue to build and run without regressions.

## Risks & Open Questions

- **Wi-Fi jitter**: UDP over Wi-Fi has 5–50ms jitter. M9's 1-second staleness threshold is conservative. Real CV pipelines will need tighter thresholds (100–200ms) and potentially a motion prediction filter — both deferred.
- **USB reverse-tethering**: `adb reverse udp:9876 udp:9876` is the simplest dev setup but USB-only. Wi-Fi deployment requires knowing the Quest's IP and ensuring both devices are on the same LAN. Document both.
- **Single server, single datagram**: one server per app instance. Each frame fits in one UDP datagram (≤28 detections). Multi-source perception and datagram fragmentation are deferred.
- **No on-screen text**: labels are logged to logcat only. Adding text rendering to the Vulkan pipeline requires a font atlas and quad-drawing shader. This is a deliberate scope limitation for M9.
- **No detection motion prediction**: 30 Hz perception frames snap to new positions every 2–3 display frames at 72 Hz. Dead reckoning or interpolation would smooth this — deferred.
- **`INTERNET` permission**: required for UDP. The Quest runtime may prompt the user on first launch. Document this.
- **Clock synchronization**: the protocol's `timestampNs` is the server's wall clock. Without NTP, the Quest-side delta is meaningless as an absolute latency metric. M9 logs the inter-frame interval as the primary latency diagnostic; absolute E2E latency measurement is deferred to M10 (which includes performance instrumentation).
- **Calibration drift**: the LOCAL space origin drifts slightly as the Quest refines its tracking. For a demo with the mock server generating LOCAL-space coordinates directly, this is negligible. For a real external camera with a static calibration, recalibration is needed after large origin shifts.

## Explicitly deferred

- On-device camera access (Camera2, passthrough RGB frame readback).
- GPU inference or on-device computer-vision pipelines.
- Multi-source sensor fusion (multiple perception servers).
- Detection tracking across frames (Kalman filter, Hungarian assignment).
- On-screen text or label rendering.
- Configurable calibration at runtime (hardcoded identity for M9).
- Datagram fragmentation for large detection sets.
- Clock synchronization (NTP/PTP) for absolute latency measurement.
