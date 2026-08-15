#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace questlab::camera {

enum class CameraSourceKind {
    MetaCamera2,
    Replay,
    ExternalRgbd,
};

enum class PixelFormat {
    Yuv420888,
    Rgba8888,
};

enum class CameraHealth {
    Stopped,
    Starting,
    Running,
    PermissionDenied,
    Unsupported,
    Error,
};

struct ImagePlane {
    std::vector<uint8_t> bytes;
    int32_t rowStride = 0;
    int32_t pixelStride = 0;
};

struct CameraIntrinsics {
    float fx = 0.0F;
    float fy = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    float skew = 0.0F;
    std::array<float, 5> distortion{};
    bool valid = false;
};

struct CameraPose {
    // Android Camera2 calibration extrinsics. orientation is
    // LENS_POSE_ROTATION (sensor/head axes to camera optical axes), while
    // position is LENS_POSE_TRANSLATION (the optical center expressed in the
    // sensor/head axes). These fields must not be composed as one rigid pose.
    std::array<float, 4> orientation = {0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 3> position{};
    bool valid = false;
};

struct RgbCapture {
    uint64_t frameId = 0;
    int64_t sensorTimestampNanoseconds = 0;
    int64_t arrivalTimestampNanoseconds = 0;
    int32_t width = 0;
    int32_t height = 0;
    PixelFormat format = PixelFormat::Yuv420888;
    std::array<ImagePlane, 3> planes;
    CameraIntrinsics intrinsics;
    CameraPose cameraFromHead;
};

struct CameraStreamConfig {
    int32_t width = 0;
    int32_t height = 0;
    int32_t framesPerSecond = 30;
};

struct CameraCapabilities {
    std::string sourceName;
    std::string cameraId;
    int32_t cameraPosition = -1;
    std::vector<CameraStreamConfig> streams;
    CameraIntrinsics intrinsics;
    CameraPose cameraFromHead;
};

struct CameraSourceStats {
    CameraHealth health = CameraHealth::Stopped;
    uint64_t receivedFrames = 0;
    uint64_t consumedFrames = 0;
    uint64_t overwrittenFrames = 0;
    uint64_t invalidFrames = 0;
    uint64_t currentQueueDepth = 0;
    uint64_t queueHighWaterMark = 0;
    std::string lastError;
};

struct CameraSourceConfig {
    CameraSourceKind kind = CameraSourceKind::MetaCamera2;
    std::string replayManifestPath;
};

struct CameraPlatformContext {
    void* javaVm = nullptr;
    void* activity = nullptr;
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

}  // namespace questlab::camera
