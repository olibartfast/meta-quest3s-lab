#include "camera_source/replay_camera_adapter.h"

#include "camera_source/fixture_manifest.h"

#include <utility>

namespace questlab::camera {

ReplayCameraAdapter::ReplayCameraAdapter(std::string manifestPath)
    : manifestPath_(std::move(manifestPath)) {}

CameraCapabilities ReplayCameraAdapter::GetCapabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

bool ReplayCameraAdapter::LoadFixture() {
    if (!LoadQuestCameraFixture(
            manifestPath_, &fixture_, &stats_.lastError)) {
        return false;
    }
    capabilities_.sourceName = "recorded-replay";
    capabilities_.cameraId = "fixture";
    capabilities_.streams = {{
        fixture_.width,
        fixture_.height,
        30,
    }};
    capabilities_.intrinsics = fixture_.intrinsics;
    capabilities_.cameraFromHead = fixture_.cameraFromHead;
    return true;
}

bool ReplayCameraAdapter::Start(const CameraStreamConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.health == CameraHealth::Running) {
        return true;
    }
    stats_ = {};
    stats_.health = CameraHealth::Starting;
    if (!LoadFixture()) {
        stats_.health = CameraHealth::Error;
        return false;
    }
    streamConfig_ = config;
    if (streamConfig_.framesPerSecond <= 0) {
        streamConfig_.framesPerSecond = 30;
    }
    nextFrameTime_ = std::chrono::steady_clock::now();
    stats_.health = CameraHealth::Running;
    return true;
}

bool ReplayCameraAdapter::TryConsumeLatest(RgbCapture* capture) {
    if (capture == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.health != CameraHealth::Running ||
        std::chrono::steady_clock::now() < nextFrameTime_) {
        return false;
    }
    *capture = fixture_;
    capture->frameId = nextFrameId_++;
    capture->arrivalTimestampNanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    capture->sensorTimestampNanoseconds =
        capture->arrivalTimestampNanoseconds;
    ++stats_.receivedFrames;
    ++stats_.consumedFrames;
    nextFrameTime_ += std::chrono::nanoseconds(
        1'000'000'000LL / streamConfig_.framesPerSecond);
    return true;
}

CameraSourceStats ReplayCameraAdapter::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void ReplayCameraAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.health = CameraHealth::Stopped;
}

}  // namespace questlab::camera
