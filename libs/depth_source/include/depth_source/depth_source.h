#pragma once

#include "depth_source/depth_math.h"
#include "xr_math/xr_math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace questlab::depth {

enum class DepthSourceKind {
    MetaEnvironmentDepth,
    Replay,
};

enum class DepthHealth {
    Stopped,
    Starting,
    Running,
    PermissionDenied,
    Unsupported,
    Error,
};

// One acquired depth view. Meta publishes two, one per eye; fusion normally
// consumes the left view alone, so views are kept separate rather than merged.
struct DepthView {
    DepthProjection projection{};
    // Pose of the depth view in the space requested at acquire time.
    math::Pose poseInSpace{};
};

// An owned, GPU-free snapshot. No OpenXR handle, swapchain index, or Vulkan
// image survives into this record: once it exists the caller may keep it for
// as long as the queue allows without holding runtime resources.
struct DepthCapture {
    uint64_t frameId = 0;
    // Predicted display time the depth was acquired for, in the OpenXR time
    // domain. Kept as-is: converting it here would hide the mapping
    // uncertainty that the correlation logic needs to reason about.
    int64_t displayTimeNanoseconds = 0;
    int64_t arrivalTimestampNanoseconds = 0;
    int32_t width = 0;
    int32_t height = 0;
    // Normalized depth as stored by the runtime, row-major, one entry per
    // texel of the view below. Linearize with DepthProjection before use.
    std::vector<float> normalizedDepth;
    DepthView view{};
};

struct DepthStreamConfig {
    // Reduces the readback and the point count by taking every Nth texel.
    // Full resolution is rarely needed for box fitting and costs bandwidth.
    int32_t sampleStride = 2;
    bool removeHands = true;
};

struct DepthCapabilities {
    std::string sourceName;
    bool supportsEnvironmentDepth = false;
    bool supportsHandRemoval = false;
    int32_t width = 0;
    int32_t height = 0;
};

struct DepthSourceStats {
    DepthHealth health = DepthHealth::Stopped;
    uint64_t acquiredFrames = 0;
    uint64_t consumedFrames = 0;
    uint64_t overwrittenFrames = 0;
    uint64_t rejectedFrames = 0;
    uint64_t currentQueueDepth = 0;
    uint64_t queueHighWaterMark = 0;
    std::string lastError;
};

class IDepthSource {
public:
    virtual ~IDepthSource() = default;

    virtual DepthCapabilities GetCapabilities() const = 0;
    virtual bool Start(const DepthStreamConfig& config) = 0;
    // Called once per OpenXR frame, between xrBeginFrame and xrEndFrame.
    // Acquiring outside that window is invalid for the Meta provider.
    virtual bool AcquireForFrame(int64_t predictedDisplayTime) = 0;
    virtual bool TryConsumeLatest(DepthCapture* capture) = 0;
    virtual DepthSourceStats GetStats() const = 0;
    virtual void Stop() = 0;
};

// Converts a capture into metric points expressed in the space the capture
// was acquired in. Invalid, near-field, and out-of-range texels are dropped
// rather than emitted as zeros, so the caller never has to filter sentinels.
void DepthCaptureToPoints(
    const DepthCapture& capture,
    int32_t sampleStride,
    float maximumRangeMeters,
    std::vector<math::Vec3>* pointsInSpace);

struct DepthSourceConfig {
    DepthSourceKind kind = DepthSourceKind::MetaEnvironmentDepth;
    std::string replayPath;
};

}  // namespace questlab::depth
