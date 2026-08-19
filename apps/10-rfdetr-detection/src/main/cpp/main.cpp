#include <android_native_app_glue.h>
#include <jni.h>

#include "camera_source/meta_camera2_adapter.h"
#include "camera_source/yuv_converter.h"
#include "detection_fusion/detection_fusion.h"
#include "depth_source/depth_source.h"
#include "depth_source/meta_environment_depth_adapter.h"
#include "detection_projection/detection_projection.h"
#include "object_detector/object_detector.h"
#include "vulkan_renderer/vulkan_stereo_renderer.h"
#include "xr_core/vulkan_session_binding.h"
#include "xr_core/xr_controller_actions.h"
#include "xr_core/xr_error.h"
#include "xr_core/xr_instance.h"
#include "xr_core/xr_session.h"
#include "xr_math/openxr_conversions.h"
#include "xr_meta_passthrough/meta_passthrough_fb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr char kModelIdentity[] =
    "1ac37bb3429380c7aee6b2c2778d20026db0ad18579cac119255d31f736dc760";
constexpr size_t kRetainedFrameCount = 16;
// Depth frames merged before fitting a box. Bounded so the point set and the
// fusion cost stay predictable; head motion limits how far back is useful.
constexpr size_t kDepthAccumulationFrames = 6;

std::atomic<questlab::camera::MetaCamera2Adapter*> gCameraAdapter{nullptr};
std::atomic<uint64_t> gFrameId{1};

struct AndroidState {
    bool resumed = false;
    bool destroyRequested = false;
};

int64_t MonotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

questlab::math::Mat4 ScaleMatrix(float x, float y, float z) {
    questlab::math::Mat4 matrix = questlab::math::IdentityMatrix();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

bool HasValidPose(XrSpaceLocationFlags flags) {
    constexpr XrSpaceLocationFlags kRequired =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (flags & kRequired) == kRequired;
}

std::vector<uint8_t> CopyByteArray(JNIEnv* environment, jbyteArray array) {
    if (environment == nullptr || array == nullptr) {
        return {};
    }
    const jsize length = environment->GetArrayLength(array);
    std::vector<uint8_t> result(static_cast<size_t>(length));
    environment->GetByteArrayRegion(
        array,
        0,
        length,
        reinterpret_cast<jbyte*>(result.data()));
    return result;
}

std::vector<float> CopyFloatArray(JNIEnv* environment, jfloatArray array) {
    if (environment == nullptr || array == nullptr) {
        return {};
    }
    const jsize length = environment->GetArrayLength(array);
    std::vector<float> result(static_cast<size_t>(length));
    environment->GetFloatArrayRegion(array, 0, length, result.data());
    return result;
}

std::string Trim(std::string value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

questlab::detection::DetectorConfig LoadDetectorConfig(
    const std::filesystem::path& privateDirectory) {
    questlab::detection::DetectorConfig config;
    config.modelPath = (privateDirectory / "rfdetr-nano.onnx").string();
    config.manifestPath = (privateDirectory / "model-manifest.json").string();
    config.expectedManifestIdentity = kModelIdentity;
    config.replayDetectionsPath =
        (privateDirectory / "replay-detections.json").string();

    const std::filesystem::path configPath =
        privateDirectory / "detector.conf";
    std::ifstream input(configPath);
    std::string line;
    while (std::getline(input, line)) {
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = Trim(line.substr(0, separator));
        const std::string value = Trim(line.substr(separator + 1U));
        try {
            if (key == "backend") {
                if (value == "streaming") {
                    config.kind =
                        questlab::detection::ObjectDetectorKind::Streaming;
                } else if (value == "replay") {
                    config.kind =
                        questlab::detection::ObjectDetectorKind::Replay;
                } else {
                    config.kind = questlab::detection::
                        ObjectDetectorKind::OnDeviceOnnxRuntime;
                }
            } else if (key == "service_host") {
                config.serviceHost = value;
            } else if (key == "service_port") {
                config.servicePort = static_cast<uint16_t>(std::stoul(value));
            } else if (key == "submission_hz") {
                config.maximumSubmissionsPerSecond = std::stof(value);
            } else if (key == "expiry_ms") {
                config.resultExpiryNanoseconds =
                    static_cast<int64_t>(std::stoll(value)) * 1'000'000;
            } else if (key == "xnnpack_threads") {
                config.xnnpackThreads = std::stoi(value);
            } else if (key == "replay_frame_id") {
                config.replayFrameId = std::stoull(value);
            }
        } catch (const std::exception& exception) {
            questlab::LogWarning(
                "Ignoring invalid detector config %s=%s: %s",
                key.c_str(),
                value.c_str(),
                exception.what());
        }
    }
    return config;
}

const char* HealthName(questlab::detection::DetectorHealth health) {
    using questlab::detection::DetectorHealth;
    switch (health) {
        case DetectorHealth::Stopped: return "STOPPED";
        case DetectorHealth::Starting: return "STARTING";
        case DetectorHealth::Running: return "RUNNING";
        case DetectorHealth::Disconnected: return "DISCONNECTED";
        case DetectorHealth::Unsupported: return "UNSUPPORTED";
        case DetectorHealth::Error: return "ERROR";
    }
    return "UNKNOWN";
}

const char* BackendName(questlab::detection::ObjectDetectorKind kind) {
    using questlab::detection::ObjectDetectorKind;
    switch (kind) {
        case ObjectDetectorKind::OnDeviceOnnxRuntime: return "ONDEVICE";
        case ObjectDetectorKind::Streaming: return "STREAMING";
        case ObjectDetectorKind::Replay: return "REPLAY";
    }
    return "UNKNOWN";
}

// Typical depth along the viewing axis, in metres, for classes this lab
// actually points at. Depth measures the near face; this fills the far one.
// Values are deliberately conservative: an over-large prior produces a box
// that swallows the wall behind the object, which reads worse than a slightly
// shallow one. Unlisted classes get no prior and keep a measured, thin box.
questlab::fusion::ObjectSizePrior SizePriorForClass(const std::string& name) {
    static const std::unordered_map<std::string, float> kDepthMeters = {
        {"person", 0.35F},      {"chair", 0.50F},
        {"couch", 0.85F},       {"bed", 2.00F},
        {"dining table", 0.90F},{"tv", 0.10F},
        {"laptop", 0.25F},      {"keyboard", 0.15F},
        {"mouse", 0.11F},       {"cell phone", 0.01F},
        {"book", 0.03F},        {"bottle", 0.07F},
        {"cup", 0.08F},         {"bowl", 0.15F},
        {"potted plant", 0.30F},{"backpack", 0.20F},
        {"vase", 0.12F},        {"clock", 0.05F},
        {"remote", 0.04F},      {"scissors", 0.02F},
    };
    questlab::fusion::ObjectSizePrior prior;
    const auto entry = kDepthMeters.find(name);
    if (entry != kDepthMeters.end()) {
        prior.valid = true;
        prior.depthMeters = entry->second;
    }
    return prior;
}

struct FrameImage {
    uint64_t frameId = 0;
    int32_t width = 0;
    int32_t height = 0;
    std::shared_ptr<const std::vector<uint8_t>> pixels;
};

struct PreparedObject {
    questlab::detection::Detection detection;
    questlab::projection::DetectionProjection projection;
    // Present only when depth supported a metric fit. A detection without a
    // box is rendered as nothing rather than as a guess.
    bool hasBox = false;
    questlab::fusion::FusedBox box;
    std::string boxRejection;
};

struct PreparedScene {
    FrameImage annotatedImage;
    std::vector<PreparedObject> objects;
    int64_t captureArrivalNanoseconds = 0;
    int64_t captureSensorNanoseconds = 0;
    int64_t publishedNanoseconds = 0;
    int64_t expiresNanoseconds = 0;
    float inferenceMilliseconds = 0.0F;
    bool poseWasSubstituted = true;
};

struct PipelineSnapshot {
    std::shared_ptr<const FrameImage> preview;
    std::shared_ptr<const PreparedScene> prepared;
    questlab::detection::DetectorStats detectorStats;
    uint64_t dispatchOverwrites = 0;
    uint64_t unmatchedResults = 0;
    uint64_t expiredResults = 0;
    uint64_t projectionFailures = 0;
    uint64_t fusionFailures = 0;
    uint64_t fusedBoxes = 0;
    uint64_t depthFramesConsumed = 0;
    size_t depthPointCount = 0;
};

class DetectionPipeline {
public:
    ~DetectionPipeline() { Stop(); }

    bool Start(const questlab::detection::DetectorConfig& config) {
        Stop();
        config_ = config;
        detector_ = questlab::detection::CreateObjectDetector(config_);
        if (detector_ == nullptr || !detector_->Start(config_)) {
            std::lock_guard<std::mutex> lock(outputMutex_);
            if (detector_ != nullptr) {
                snapshot_.detectorStats = detector_->GetStats();
            }
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(inputMutex_);
            stopping_ = false;
            pendingCapture_.reset();
        }
        worker_ = std::thread(&DetectionPipeline::WorkerMain, this);
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(inputMutex_);
            stopping_ = true;
            pendingCapture_.reset();
        }
        inputCondition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (detector_ != nullptr) {
            detector_->Stop();
            std::lock_guard<std::mutex> lock(outputMutex_);
            snapshot_.detectorStats = detector_->GetStats();
        }
        records_.clear();
    }

    void SubmitCapture(
        questlab::camera::RgbCapture capture,
        const questlab::math::Pose& localFromHead) {
        PendingCapture pending{
            std::move(capture),
            localFromHead,
        };
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (stopping_) {
            return;
        }
        if (pendingCapture_.has_value()) {
            ++dispatchOverwrites_;
        }
        pendingCapture_ = std::move(pending);
        inputCondition_.notify_one();
    }

    // Called from the frame loop with an owned depth snapshot. Conversion to
    // metric points and fusion both happen on the worker: they are O(texels)
    // and O(points) and must never run on the render thread.
    void SubmitDepth(questlab::depth::DepthCapture capture) {
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (stopping_) {
            return;
        }
        pendingDepth_ = std::move(capture);
        inputCondition_.notify_one();
    }

    PipelineSnapshot Snapshot() const {
        std::lock_guard<std::mutex> lock(outputMutex_);
        return snapshot_;
    }

private:
    struct PendingCapture {
        questlab::camera::RgbCapture capture;
        questlab::math::Pose localFromHead;
    };

    struct RetainedFrame {
        uint64_t frameId = 0;
        int64_t captureTimestampNanoseconds = 0;
        int64_t arrivalNanoseconds = 0;
        int32_t width = 0;
        int32_t height = 0;
        questlab::camera::CameraIntrinsics intrinsics;
        questlab::camera::CameraPose cameraCalibration;
        questlab::math::Pose localFromHead;
        std::shared_ptr<const std::vector<uint8_t>> rgba;
    };

    void WorkerMain() {
        while (true) {
            std::optional<PendingCapture> pending;
            {
                std::unique_lock<std::mutex> lock(inputMutex_);
                inputCondition_.wait_for(
                    lock,
                    std::chrono::milliseconds(5),
                    [&]() { return stopping_ || pendingCapture_.has_value(); });
                if (stopping_) {
                    return;
                }
                if (pendingCapture_.has_value()) {
                    pending = std::move(pendingCapture_);
                    pendingCapture_.reset();
                }
            }
            std::optional<questlab::depth::DepthCapture> depth;
            {
                std::lock_guard<std::mutex> lock(inputMutex_);
                if (pendingDepth_.has_value()) {
                    depth = std::move(pendingDepth_);
                    pendingDepth_.reset();
                }
            }
            if (depth.has_value()) {
                ProcessDepth(*depth);
            }
            if (pending.has_value()) {
                ProcessCapture(std::move(*pending));
            }
            questlab::detection::DetectionResult result;
            while (detector_ != nullptr &&
                   detector_->TryConsumeLatest(&result)) {
                ProcessResult(std::move(result));
            }
            PublishStats();
        }
    }

    void ProcessDepth(const questlab::depth::DepthCapture& capture) {
        // Points are expressed in LOCAL, so for static geometry successive
        // captures describe the same world surfaces and may be accumulated.
        // One frame at stride 2 leaves only a few hundred samples inside a
        // typical detection, which is what makes the fitted box coarse. The
        // sensor supplies ~30 frames a second and all but one were discarded.
        questlab::depth::DepthCaptureToPoints(
            capture,
            depthSampleStride_,
            fusionParameters_.maximumRangeMeters,
            &latestDepthPoints_);
        depthHistory_.push_back(latestDepthPoints_);
        while (depthHistory_.size() > kDepthAccumulationFrames) {
            depthHistory_.pop_front();
        }
        depthPointsInLocal_.clear();
        size_t total = 0;
        for (const auto& frame : depthHistory_) {
            total += frame.size();
        }
        depthPointsInLocal_.reserve(total);
        for (const auto& frame : depthHistory_) {
            depthPointsInLocal_.insert(
                depthPointsInLocal_.end(), frame.begin(), frame.end());
        }
        ++depthFramesConsumed_;

        // The stored-depth convention and texel format are assumptions this
        // code cannot query, and a wrong one yields zero usable points with
        // no other symptom. Report the raw distribution so one run on the
        // headset settles it instead of another round of guessing.
        const int64_t now = MonotonicNanoseconds();
        if (now - lastDepthReportNanoseconds_ < 1'000'000'000) {
            return;
        }
        lastDepthReportNanoseconds_ = now;
        float rawMinimum = 1.0F;
        float rawMaximum = 0.0F;
        size_t zeroTexels = 0;
        size_t oneTexels = 0;
        for (const float value : capture.normalizedDepth) {
            rawMinimum = std::min(rawMinimum, value);
            rawMaximum = std::max(rawMaximum, value);
            zeroTexels += value <= 0.0F ? 1U : 0U;
            oneTexels += value >= 1.0F ? 1U : 0U;
        }
        // Centre probe: median of a small patch at the image centre, in
        // metres. This is the only figure that can be checked against a tape
        // measure, and it separates a noisy sensor from a wrong conversion.
        // A patch median rather than a single texel, so sensor noise does not
        // masquerade as a calibration error.
        std::vector<float> centrePatch;
        const int32_t cx = capture.width / 2;
        const int32_t cy = capture.height / 2;
        for (int32_t dy = -3; dy <= 3; ++dy) {
            for (int32_t dx = -3; dx <= 3; ++dx) {
                const int32_t px = cx + dx;
                const int32_t py = cy + dy;
                if (px < 0 || py < 0 || px >= capture.width ||
                    py >= capture.height) {
                    continue;
                }
                const size_t index = static_cast<size_t>(py) *
                                     static_cast<size_t>(capture.width) +
                                 static_cast<size_t>(px);
                float metres = 0.0F;
                if (questlab::depth::LinearizeDepth(
                        capture.normalizedDepth[index],
                        capture.view.projection,
                        &metres)) {
                    centrePatch.push_back(metres);
                }
            }
        }
        float centreMetres = -1.0F;
        float patchSpread = -1.0F;
        if (!centrePatch.empty()) {
            std::sort(centrePatch.begin(), centrePatch.end());
            centreMetres = centrePatch[centrePatch.size() / 2];
            patchSpread = centrePatch.back() - centrePatch.front();
        }

        // Linearize the extremes under the current assumption so the log
        // shows metres, which is what can be checked against a tape measure.
        float nearMetres = 0.0F;
        float farMetres = 0.0F;
        const bool nearOk = questlab::depth::LinearizeDepth(
            rawMinimum, capture.view.projection, &nearMetres);
        const bool farOk = questlab::depth::LinearizeDepth(
            rawMaximum, capture.view.projection, &farMetres);
        questlab::LogInfo(
            "DEPTH_DIAG %dx%d nearZ=%.4f farZ=%.4f raw=[%.5f %.5f] "
            "zero=%zu one=%zu points=%zu metres=[%s %s] "
            "CENTRE=%.3fm spread=%.3fm",
            capture.width,
            capture.height,
            capture.view.projection.nearZ,
            capture.view.projection.farZ,
            rawMinimum,
            rawMaximum,
            zeroTexels,
            oneTexels,
            depthPointsInLocal_.size(),
            nearOk ? std::to_string(nearMetres).c_str() : "rejected",
            farOk ? std::to_string(farMetres).c_str() : "rejected",
            centreMetres,
            patchSpread);
    }

    void ProcessCapture(PendingCapture pending) {
        auto rgba = std::make_shared<std::vector<uint8_t>>();
        if (!questlab::camera::ConvertYuv420ToRgba(
                pending.capture, rgba.get())) {
            return;
        }
        auto preview = std::make_shared<FrameImage>();
        preview->frameId = pending.capture.frameId;
        preview->width = pending.capture.width;
        preview->height = pending.capture.height;
        preview->pixels = rgba;
        {
            std::lock_guard<std::mutex> lock(outputMutex_);
            snapshot_.preview = std::move(preview);
        }

        RetainedFrame record;
        record.frameId = pending.capture.frameId;
        record.captureTimestampNanoseconds =
            pending.capture.sensorTimestampNanoseconds;
        record.arrivalNanoseconds =
            pending.capture.arrivalTimestampNanoseconds > 0
                ? pending.capture.arrivalTimestampNanoseconds
                : MonotonicNanoseconds();
        record.width = pending.capture.width;
        record.height = pending.capture.height;
        record.intrinsics = pending.capture.intrinsics;
        record.cameraCalibration = pending.capture.cameraFromHead;
        record.localFromHead = pending.localFromHead;
        record.rgba = rgba;
        if (detector_ != nullptr &&
            detector_->Submit(std::move(pending.capture))) {
            records_.push_back(std::move(record));
            while (records_.size() > kRetainedFrameCount) {
                records_.pop_front();
            }
        }
    }

    void ProcessResult(questlab::detection::DetectionResult result) {
        const auto record = std::find_if(
            records_.begin(),
            records_.end(),
            [&](const RetainedFrame& candidate) {
                return candidate.frameId == result.frameId;
            });
        if (record == records_.end() ||
            result.manifestIdentity != config_.expectedManifestIdentity ||
            result.sourceWidth != record->width ||
            result.sourceHeight != record->height) {
            ++unmatchedResults_;
            return;
        }
        const int64_t now = MonotonicNanoseconds();
        if (now - record->arrivalNanoseconds >
            config_.resultExpiryNanoseconds) {
            ++expiredResults_;
            return;
        }

        auto prepared = std::make_shared<PreparedScene>();
        prepared->captureArrivalNanoseconds = record->arrivalNanoseconds;
        prepared->captureSensorNanoseconds =
            record->captureTimestampNanoseconds;
        prepared->publishedNanoseconds = now;
        prepared->expiresNanoseconds =
            record->arrivalNanoseconds + config_.resultExpiryNanoseconds;
        prepared->inferenceMilliseconds = static_cast<float>(
            result.inferenceEndNanoseconds -
            result.inferenceStartNanoseconds) / 1.0e6F;
        prepared->poseWasSubstituted = true;
        bool projectionSummaryLogged = false;
        for (const questlab::detection::Detection& detection :
             result.detections) {
            questlab::projection::DetectionProjection projected;
            std::string error;
            if (!questlab::projection::BuildDetectionProjection(
                    detection.boxXyxy,
                    record->width,
                    record->height,
                    record->intrinsics,
                    record->cameraCalibration,
                    record->localFromHead,
                    &projected,
                    &error)) {
                ++projectionFailures_;
                questlab::LogWarning(
                    "Projection rejected frame=%llu class=%s: %s",
                    static_cast<unsigned long long>(result.frameId),
                    detection.className.c_str(),
                    error.c_str());
                continue;
            }
            PreparedObject object;
            object.detection = detection;
            object.projection = projected;
            // Fusion runs here, on the worker, over the most recent depth
            // point set. A detection older than the depth is still usable:
            // both are expressed in LOCAL, so no pixel registration is
            // needed, only that the poses were valid when captured.
            questlab::fusion::DetectionFrustum frustum;
            frustum.originInLocal = projected.originInLocal;
            frustum.cornerDirectionsInLocal =
                projected.cornerDirectionsInLocal;
            frustum.centerDirectionInLocal = projected.centerDirectionInLocal;
            std::string boxReason;
            if (questlab::fusion::FuseDetection(
                    frustum,
                    depthPointsInLocal_,
                    fusionParameters_,
                    SizePriorForClass(detection.className),
                    &object.box,
                    &boxReason)) {
                object.hasBox = true;
                ++fusedBoxes_;
            } else {
                object.boxRejection = boxReason;
                ++fusionFailures_;
            }
            prepared->objects.push_back(std::move(object));
            if (!projectionSummaryLogged) {
                projectionSummaryLogged = true;
                questlab::LogInfo(
                    "WORLD_PROJECTION frame=%llu camera_rotation=[%.4f %.4f "
                    "%.4f %.4f] camera_position=[%.4f %.4f %.4f] "
                    "local_origin=[%.3f %.3f %.3f] local_direction=[%.3f "
                    "%.3f %.3f]",
                    static_cast<unsigned long long>(result.frameId),
                    record->cameraCalibration.orientation[0],
                    record->cameraCalibration.orientation[1],
                    record->cameraCalibration.orientation[2],
                    record->cameraCalibration.orientation[3],
                    record->cameraCalibration.position[0],
                    record->cameraCalibration.position[1],
                    record->cameraCalibration.position[2],
                    projected.originInLocal.x,
                    projected.originInLocal.y,
                    projected.originInLocal.z,
                    projected.centerDirectionInLocal.x,
                    projected.centerDirectionInLocal.y,
                    projected.centerDirectionInLocal.z);
            }
        }

        auto annotated = std::make_shared<std::vector<uint8_t>>(*record->rgba);
        const questlab::detection::DetectorStats stats =
            detector_->GetStats();
        const int64_t ageMilliseconds =
            (now - record->arrivalNanoseconds) / 1'000'000;
        std::vector<std::string> statusLines;
        statusLines.push_back(
            std::string("BACKEND ") + BackendName(config_.kind) + " " +
            HealthName(stats.health));
        {
            size_t boxed = 0;
            for (const PreparedObject& object : prepared->objects) {
                boxed += object.hasBox ? 1U : 0U;
            }
            statusLines.push_back(
                "BOXES " + std::to_string(boxed) + "/" +
                std::to_string(prepared->objects.size()));
        }
        statusLines.push_back(
            "AGE " + std::to_string(ageMilliseconds) + " MS INFERENCE " +
            std::to_string(static_cast<int>(std::lround(
                prepared->inferenceMilliseconds))) + " MS");
        statusLines.push_back(
            "SUBMITTED " + std::to_string(stats.submittedFrames) +
            " DROPPED " + std::to_string(
                stats.replacedPendingFrames + stats.rateLimitedFrames +
                dispatchOverwrites_));
        std::string annotationError;
        if (!questlab::detection::AnnotateRgba(
                record->width,
                record->height,
                annotated.get(),
                result.detections,
                statusLines,
                &annotationError)) {
            questlab::LogWarning(
                "Diagnostic annotation failed: %s",
                annotationError.c_str());
        }
        prepared->annotatedImage = {
            record->frameId,
            record->width,
            record->height,
            annotated,
        };
        for (const auto& detection : result.detections) {
            questlab::LogInfo(
                "DETECTION frame=%llu class_id=%d class=%s confidence=%.3f "
                "box=[%.1f %.1f %.1f %.1f] age_ms=%lld inference_ms=%.1f",
                static_cast<unsigned long long>(result.frameId),
                detection.classId,
                detection.className.c_str(),
                detection.confidence,
                detection.boxXyxy[0],
                detection.boxXyxy[1],
                detection.boxXyxy[2],
                detection.boxXyxy[3],
                static_cast<long long>(ageMilliseconds),
                prepared->inferenceMilliseconds);
        }
        {
            std::lock_guard<std::mutex> lock(outputMutex_);
            snapshot_.prepared = std::move(prepared);
        }
        records_.erase(records_.begin(), std::next(record));
    }

    void PublishStats() {
        std::lock_guard<std::mutex> lock(outputMutex_);
        if (detector_ != nullptr) {
            snapshot_.detectorStats = detector_->GetStats();
        }
        snapshot_.dispatchOverwrites = dispatchOverwrites_;
        snapshot_.unmatchedResults = unmatchedResults_;
        snapshot_.expiredResults = expiredResults_;
        snapshot_.projectionFailures = projectionFailures_;
        snapshot_.fusionFailures = fusionFailures_;
        snapshot_.fusedBoxes = fusedBoxes_;
        snapshot_.depthFramesConsumed = depthFramesConsumed_;
        snapshot_.depthPointCount = depthPointsInLocal_.size();
    }

    questlab::detection::DetectorConfig config_;
    std::unique_ptr<questlab::detection::IObjectDetector> detector_;
    std::thread worker_;
    mutable std::mutex inputMutex_;
    std::condition_variable inputCondition_;
    std::optional<PendingCapture> pendingCapture_;
    bool stopping_ = true;
    std::deque<RetainedFrame> records_;
    mutable std::mutex outputMutex_;
    PipelineSnapshot snapshot_;
    std::optional<questlab::depth::DepthCapture> pendingDepth_;
    std::vector<questlab::math::Vec3> depthPointsInLocal_;
    std::vector<questlab::math::Vec3> latestDepthPoints_;
    std::deque<std::vector<questlab::math::Vec3>> depthHistory_;
    questlab::fusion::FusionParameters fusionParameters_;
    int32_t depthSampleStride_ = 1;
    uint64_t fusionFailures_ = 0;
    uint64_t fusedBoxes_ = 0;
    uint64_t depthFramesConsumed_ = 0;
    int64_t lastDepthReportNanoseconds_ = 0;
    uint64_t dispatchOverwrites_ = 0;
    uint64_t unmatchedResults_ = 0;
    uint64_t expiredResults_ = 0;
    uint64_t projectionFailures_ = 0;
};

class DetectionScene final :
    public questlab::XrFrameUpdater,
    public questlab::VulkanSceneProvider {
public:
    DetectionScene(
        questlab::camera::IRgbCameraSource* source,
        questlab::detection::DetectorConfig detectorConfig)
        : source_(source), detectorConfig_(std::move(detectorConfig)) {}

    bool Initialize(XrInstance instance, XrSession session) {
        return actions_.Initialize(instance, session);
    }

    // Depth is optional: if the system or runtime does not provide it the app
    // still detects and still runs, it just draws no boxes. Failing the whole
    // application over a missing sensor would be the wrong trade.
    void InitializeDepth(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session,
        XrSpace localSpace,
        const questlab::VulkanDeviceContext& deviceContext) {
        bool supportsDepth = false;
        bool supportsHandRemoval = false;
        if (!questlab::depth::MetaEnvironmentDepthAdapter::QuerySupport(
                instance, systemId, &supportsDepth, &supportsHandRemoval) ||
            !supportsDepth) {
            questlab::LogWarning(
                "Environment depth is unsupported; 3D boxes are disabled");
            return;
        }
        auto adapter =
            std::make_unique<questlab::depth::MetaEnvironmentDepthAdapter>();
        if (!adapter->Initialize(
                instance, session, localSpace, deviceContext)) {
            questlab::LogError(
                "Environment depth init failed: %s",
                adapter->GetStats().lastError.c_str());
            return;
        }
        questlab::depth::DepthStreamConfig depthConfig;
        depthConfig.removeHands = supportsHandRemoval;
        if (!adapter->Start(depthConfig)) {
            questlab::LogError(
                "Environment depth start failed: %s",
                adapter->GetStats().lastError.c_str());
            return;
        }
        const auto capabilities = adapter->GetCapabilities();
        questlab::LogInfo(
            "Environment depth running %dx%d hand_removal=%s",
            capabilities.width,
            capabilities.height,
            supportsHandRemoval ? "on" : "off");
        depth_ = std::move(adapter);
    }

    bool StartDetector() {
        if (detectorRunning_) {
            return true;
        }
        detectorRunning_ = pipeline_.Start(detectorConfig_);
        questlab::LogInfo(
            "Detector start backend=%s result=%s model_identity=%s",
            BackendName(detectorConfig_.kind),
            detectorRunning_ ? "accepted" : "failed",
            detectorConfig_.expectedManifestIdentity.c_str());
        return detectorRunning_;
    }

    void StopDetector() {
        if (!detectorRunning_) {
            return;
        }
        pipeline_.Stop();
        detectorRunning_ = false;
    }

    bool UpdateFrame(const questlab::XrFrameUpdateInfo& frame) override {
        if (!actions_.UpdateFrame(frame)) {
            return false;
        }
        // Acquisition is only valid between xrBeginFrame and xrEndFrame, and
        // the frame updater runs inside that window. The consume below only
        // polls a fence, so neither call blocks the render loop.
        if (depth_ != nullptr) {
            depth_->AcquireForFrame(frame.predictedDisplayTime);
            const auto depthStats = depth_->GetStats();
            const int64_t nowNs = MonotonicNanoseconds();
            if (nowNs - lastDepthStatsNanoseconds_ >= 1'000'000'000) {
                lastDepthStatsNanoseconds_ = nowNs;
                questlab::LogInfo(
                    "DEPTH_SRC acquired=%llu consumed=%llu rejected=%llu "
                    "overwritten=%llu last_error=%s",
                    static_cast<unsigned long long>(depthStats.acquiredFrames),
                    static_cast<unsigned long long>(depthStats.consumedFrames),
                    static_cast<unsigned long long>(depthStats.rejectedFrames),
                    static_cast<unsigned long long>(
                        depthStats.overwrittenFrames),
                    depthStats.lastError.c_str());
            }
            questlab::depth::DepthCapture depthCapture;
            if (depth_->TryConsumeLatest(&depthCapture)) {
                pipeline_.SubmitDepth(std::move(depthCapture));
            }
        }
        const questlab::XrControllerState& right =
            actions_.State(questlab::XrHand::Right);
        const bool togglePressed = right.secondaryActive && right.secondary;
        if (togglePressed && !toggleWasPressed_) {
            previewEnabled_ = !previewEnabled_;
            questlab::LogInfo(
                "2D diagnostic preview: %s",
                previewEnabled_ ? "visible" : "hidden");
        }
        toggleWasPressed_ = togglePressed;
        return true;
    }

    bool BuildScene(
        const questlab::XrFrameRenderInfo& frame,
        std::vector<questlab::DebugLineDraw>* draws) override {
        if (draws == nullptr || source_ == nullptr) {
            return false;
        }
        if (HasValidPose(frame.headInLocal.locationFlags)) {
            latestLocalFromHead_ =
                questlab::math::FromXr(frame.headInLocal.pose);
            headPoseValid_ = true;
        }
        questlab::camera::RgbCapture capture;
        if (headPoseValid_ && source_->TryConsumeLatest(&capture)) {
            pipeline_.SubmitCapture(
                std::move(capture), latestLocalFromHead_);
        }

        const PipelineSnapshot snapshot = pipeline_.Snapshot();
        const int64_t now = MonotonicNanoseconds();
        const bool preparedValid = snapshot.prepared != nullptr &&
            now <= snapshot.prepared->expiresNanoseconds;
        const std::shared_ptr<const FrameImage> displayed =
            preparedValid
                ? std::make_shared<FrameImage>(
                    snapshot.prepared->annotatedImage)
                : snapshot.preview;
        if (previewEnabled_ && displayed != nullptr &&
            displayed->pixels != nullptr && headPoseValid_) {
            const float aspect = static_cast<float>(displayed->width) /
                                 static_cast<float>(displayed->height);
            image_.frameId = displayed->frameId;
            image_.width = static_cast<uint32_t>(displayed->width);
            image_.height = static_cast<uint32_t>(displayed->height);
            image_.pixels = displayed->pixels;
            image_.model = questlab::math::Multiply(
                questlab::math::PoseMatrix(latestLocalFromHead_),
                questlab::math::Multiply(
                    questlab::math::TranslationMatrix(
                        {0.0F, -0.35F, -1.25F}),
                    ScaleMatrix(0.42F * aspect, 0.42F, 1.0F)));
            hasImage_ = true;
        } else {
            hasImage_ = false;
        }

        if (preparedValid) {
            std::vector<const PreparedObject*> ordered;
            ordered.reserve(snapshot.prepared->objects.size());
            for (const PreparedObject& object : snapshot.prepared->objects) {
                ordered.push_back(&object);
            }
            std::sort(
                ordered.begin(),
                ordered.end(),
                [&](const PreparedObject* first, const PreparedObject* second) {
                    // The renderer has no depth attachment, so draw order is
                    // the only occlusion control. With a measured range,
                    // farthest first puts nearer boxes on top; without one,
                    // fall back to confidence.
                    if (first->hasBox && second->hasBox) {
                        return first->box.representativeRangeMeters >
                               second->box.representativeRangeMeters;
                    }
                    if (first->hasBox != second->hasBox) {
                        return !first->hasBox;
                    }
                    return first->detection.confidence <
                           second->detection.confidence;
                });
            const float ageRatio = std::clamp(
                static_cast<float>(now -
                    snapshot.prepared->captureArrivalNanoseconds) /
                    static_cast<float>(detectorConfig_.resultExpiryNanoseconds),
                0.0F,
                1.0F);
            for (const PreparedObject* object : ordered) {
                const float ageBrightness = 1.0F - 0.70F * ageRatio;
                const std::array<float, 4> color = {
                    ageBrightness,
                    ageBrightness *
                        (0.25F + 0.65F * object->detection.confidence),
                    0.05F * ageBrightness,
                    1.0F,
                };
                if (object->hasBox) {
                    // A measured cuboid: centre, extents, and orientation all
                    // come from depth. DebugLineShape::Box is a unit cube, so
                    // the model matrix scales by the full extents.
                    const questlab::fusion::FusedBox& fused = object->box;
                    // A box whose depth came from a prior is tinted so the
                    // assumed dimension stays legible as an assumption.
                    std::array<float, 4> boxColor = color;
                    if (fused.depthExtentIsAssumed) {
                        boxColor[2] = 0.55F * ageBrightness;
                    }
                    draws->push_back({
                        questlab::DebugLineShape::Box,
                        questlab::math::Multiply(
                            questlab::math::Multiply(
                                questlab::math::TranslationMatrix(
                                    fused.centerInLocal),
                                questlab::math::RotationMatrix(
                                    fused.orientationInLocal)),
                            ScaleMatrix(
                                fused.halfExtents.x * 2.0F,
                                fused.halfExtents.y * 2.0F,
                                fused.halfExtents.z * 2.0F)),
                        boxColor,
                    });
                }
                // No measured box means nothing is drawn. Bearing rays were
                // tried and rejected: they emanate from the viewer's own eye
                // position, so they read as a starburst rather than as a
                // located object. Drawing nothing keeps a depth failure
                // visible instead of disguising it.
            }
        }

        if (previewEnabled_ && headPoseValid_) {
            const auto health = snapshot.detectorStats.health;
            const std::array<float, 4> statusColor =
                health == questlab::detection::DetectorHealth::Running
                    ? std::array<float, 4>{0.1F, 1.0F, 0.2F, 1.0F}
                    : health == questlab::detection::DetectorHealth::Starting
                        ? std::array<float, 4>{1.0F, 0.8F, 0.1F, 1.0F}
                        : std::array<float, 4>{1.0F, 0.15F, 0.1F, 1.0F};
            draws->push_back({
                questlab::DebugLineShape::ScreenRectangle,
                questlab::math::Multiply(
                    questlab::math::PoseMatrix(latestLocalFromHead_),
                    questlab::math::Multiply(
                        questlab::math::TranslationMatrix(
                            {0.0F, -0.35F, -1.24F}),
                        ScaleMatrix(0.78F, 0.42F, 1.0F))),
                statusColor,
            });
        }
        Report(snapshot, preparedValid);
        return true;
    }

    bool GetRgbaImageQuad(questlab::RgbaImageQuad* image) override {
        if (image == nullptr || !hasImage_) {
            return false;
        }
        *image = image_;
        return true;
    }

    void Shutdown() {
        StopDetector();
        if (depth_ != nullptr) {
            depth_->Stop();
            depth_.reset();
        }
        actions_.Shutdown();
    }

private:
    void Report(const PipelineSnapshot& snapshot, bool preparedValid) {
        const int64_t now = MonotonicNanoseconds();
        if (now - lastReportNanoseconds_ < 1'000'000'000) {
            return;
        }
        lastReportNanoseconds_ = now;
        const auto& stats = snapshot.detectorStats;
        questlab::LogInfo(
            "RFDETR status backend=%s health=%s prepared=%s "
            "boxes=%llu fusion_fail=%llu depth_frames=%llu depth_points=%zu "
            "submitted=%llu completed=%llu pending_drop=%llu rate_drop=%llu "
            "dispatch_drop=%llu unmatched=%llu expired=%llu projection_fail=%llu "
            "error=%s",
            BackendName(detectorConfig_.kind),
            HealthName(stats.health),
            preparedValid ? "visible" : "none",
            static_cast<unsigned long long>(snapshot.fusedBoxes),
            static_cast<unsigned long long>(snapshot.fusionFailures),
            static_cast<unsigned long long>(snapshot.depthFramesConsumed),
            snapshot.depthPointCount,
            static_cast<unsigned long long>(stats.submittedFrames),
            static_cast<unsigned long long>(stats.completedFrames),
            static_cast<unsigned long long>(stats.replacedPendingFrames),
            static_cast<unsigned long long>(stats.rateLimitedFrames),
            static_cast<unsigned long long>(snapshot.dispatchOverwrites),
            static_cast<unsigned long long>(snapshot.unmatchedResults),
            static_cast<unsigned long long>(snapshot.expiredResults),
            static_cast<unsigned long long>(snapshot.projectionFailures),
            stats.lastError.c_str());
        if (preparedValid && snapshot.prepared != nullptr) {
            const auto& scene = *snapshot.prepared;
            const double pipelineMs =
                static_cast<double>(
                    scene.publishedNanoseconds - scene.captureArrivalNanoseconds)
                / 1.0e6;
            const double ageAtReportMs =
                static_cast<double>(now - scene.publishedNanoseconds) / 1.0e6;
            const double captureToNowMs =
                static_cast<double>(now - scene.captureArrivalNanoseconds)
                / 1.0e6;
            worstCaptureToNowMs_ =
                std::max(worstCaptureToNowMs_, captureToNowMs);
            // Separates the two candidate causes of misplacement: a large
            // pipeline time means the box describes a frame the user has
            // already moved away from, while a large age means the box is
            // simply being held on screen too long after that.
            questlab::LogInfo(
                "LATENCY pipeline=%.1fms inference=%.1fms age_on_screen=%.1fms "
                "capture_to_now=%.1fms worst=%.1fms objects=%zu",
                pipelineMs,
                static_cast<double>(scene.inferenceMilliseconds),
                ageAtReportMs,
                captureToNowMs,
                worstCaptureToNowMs_,
                scene.objects.size());
        }
        if (!stats.backendDetails.empty() && !backendDetailsLogged_) {
            backendDetailsLogged_ = true;
            questlab::LogInfo("Detector runtime: %s", stats.backendDetails.c_str());
        }
    }

    std::unique_ptr<questlab::depth::IDepthSource> depth_;
    int64_t lastDepthStatsNanoseconds_ = 0;
    double worstCaptureToNowMs_ = 0.0;
    questlab::camera::IRgbCameraSource* source_ = nullptr;
    questlab::detection::DetectorConfig detectorConfig_;
    DetectionPipeline pipeline_;
    questlab::XrControllerActions actions_;
    questlab::math::Pose latestLocalFromHead_;
    questlab::RgbaImageQuad image_;
    int64_t lastReportNanoseconds_ = 0;
    bool detectorRunning_ = false;
    bool headPoseValid_ = false;
    bool previewEnabled_ = false;
    bool toggleWasPressed_ = false;
    bool hasImage_ = false;
    bool backendDetailsLogged_ = false;
};

void HandleAppCommand(android_app* app, int32_t command) {
    auto* state = static_cast<AndroidState*>(app->userData);
    switch (command) {
        case APP_CMD_RESUME:
            state->resumed = true;
            questlab::LogInfo("Android lifecycle: RESUME");
            break;
        case APP_CMD_PAUSE:
            state->resumed = false;
            questlab::LogInfo("Android lifecycle: PAUSE");
            break;
        case APP_CMD_DESTROY:
            state->destroyRequested = true;
            questlab::LogInfo("Android lifecycle: DESTROY");
            break;
        default:
            break;
    }
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnPermissionState(
    JNIEnv*,
    jobject,
    jboolean granted) {
    if (auto* adapter = gCameraAdapter.load()) {
        adapter->SetPermissionState(granted == JNI_TRUE);
        questlab::LogInfo(
            "Camera permission: %s",
            granted == JNI_TRUE ? "granted" : "denied");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraConfigured(
    JNIEnv* environment,
    jobject,
    jstring cameraId,
    jint position,
    jint width,
    jint height,
    jfloatArray intrinsics,
    jfloatArray distortion,
    jfloatArray rotation,
    jfloatArray translation) {
    auto* adapter = gCameraAdapter.load();
    if (adapter == nullptr) {
        return;
    }
    questlab::camera::CameraCapabilities capabilities;
    capabilities.sourceName = "Meta Camera2 passthrough";
    const char* cameraIdChars =
        environment->GetStringUTFChars(cameraId, nullptr);
    if (cameraIdChars != nullptr) {
        capabilities.cameraId = cameraIdChars;
        environment->ReleaseStringUTFChars(cameraId, cameraIdChars);
    }
    capabilities.cameraPosition = position;
    capabilities.streams.push_back({width, height, 30});
    const std::vector<float> intrinsicValues =
        CopyFloatArray(environment, intrinsics);
    if (intrinsicValues.size() >= 5U) {
        capabilities.intrinsics.fx = intrinsicValues[0];
        capabilities.intrinsics.fy = intrinsicValues[1];
        capabilities.intrinsics.cx = intrinsicValues[2];
        capabilities.intrinsics.cy = intrinsicValues[3];
        capabilities.intrinsics.skew = intrinsicValues[4];
        capabilities.intrinsics.valid = true;
    }
    const std::vector<float> distortionValues =
        CopyFloatArray(environment, distortion);
    std::copy_n(
        distortionValues.begin(),
        std::min(
            distortionValues.size(),
            capabilities.intrinsics.distortion.size()),
        capabilities.intrinsics.distortion.begin());
    const std::vector<float> rotationValues =
        CopyFloatArray(environment, rotation);
    const std::vector<float> translationValues =
        CopyFloatArray(environment, translation);
    if (rotationValues.size() >= 4U && translationValues.size() >= 3U) {
        std::copy_n(
            rotationValues.begin(),
            4,
            capabilities.cameraFromHead.orientation.begin());
        std::copy_n(
            translationValues.begin(),
            3,
            capabilities.cameraFromHead.position.begin());
        capabilities.cameraFromHead.valid = true;
    }
    adapter->OnConfigured(capabilities);
    questlab::LogInfo(
        "Selected camera id=%s position=%d stream=%dx%d intrinsics=%s pose=%s "
        "K=[%.3f %.3f %.3f %.3f %.3f] camera_rotation=[%.5f %.5f %.5f "
        "%.5f] camera_position=[%.5f %.5f %.5f]",
        capabilities.cameraId.c_str(),
        capabilities.cameraPosition,
        width,
        height,
        capabilities.intrinsics.valid ? "valid" : "unavailable",
        capabilities.cameraFromHead.valid ? "valid" : "unavailable",
        capabilities.intrinsics.fx,
        capabilities.intrinsics.fy,
        capabilities.intrinsics.cx,
        capabilities.intrinsics.cy,
        capabilities.intrinsics.skew,
        capabilities.cameraFromHead.orientation[0],
        capabilities.cameraFromHead.orientation[1],
        capabilities.cameraFromHead.orientation[2],
        capabilities.cameraFromHead.orientation[3],
        capabilities.cameraFromHead.position[0],
        capabilities.cameraFromHead.position[1],
        capabilities.cameraFromHead.position[2]);
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraFrame(
    JNIEnv* environment,
    jobject,
    jlong timestampNanoseconds,
    jint width,
    jint height,
    jbyteArray y,
    jint yRowStride,
    jint yPixelStride,
    jbyteArray u,
    jint uRowStride,
    jint uPixelStride,
    jbyteArray v,
    jint vRowStride,
    jint vPixelStride) {
    auto* adapter = gCameraAdapter.load();
    if (adapter == nullptr) {
        return;
    }
    questlab::camera::RgbCapture capture;
    capture.frameId = gFrameId.fetch_add(1);
    capture.sensorTimestampNanoseconds = timestampNanoseconds;
    capture.arrivalTimestampNanoseconds = MonotonicNanoseconds();
    capture.width = width;
    capture.height = height;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0] = {
        CopyByteArray(environment, y), yRowStride, yPixelStride};
    capture.planes[1] = {
        CopyByteArray(environment, u), uRowStride, uPixelStride};
    capture.planes[2] = {
        CopyByteArray(environment, v), vRowStride, vPixelStride};
    const questlab::camera::CameraCapabilities capabilities =
        adapter->GetCapabilities();
    capture.intrinsics = capabilities.intrinsics;
    capture.cameraFromHead = capabilities.cameraFromHead;
    adapter->OnFrame(std::move(capture));
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCameraError(
    JNIEnv* environment,
    jobject,
    jstring message) {
    const char* messageChars =
        environment->GetStringUTFChars(message, nullptr);
    const std::string error =
        messageChars != nullptr ? messageChars : "Unknown Camera2 error";
    if (messageChars != nullptr) {
        environment->ReleaseStringUTFChars(message, messageChars);
    }
    if (auto* adapter = gCameraAdapter.load()) {
        adapter->OnError(error);
    }
    questlab::LogError("%s", error.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_olibartfast_questlab_questcamera_QuestCameraActivity_nativeOnCaptureSaved(
    JNIEnv* environment,
    jobject,
    jstring path,
    jlong byteCount) {
    const char* pathChars =
        environment->GetStringUTFChars(path, nullptr);
    questlab::LogInfo(
        "Private camera capture saved: path=%s bytes=%lld",
        pathChars != nullptr ? pathChars : "unknown",
        static_cast<long long>(byteCount));
    if (pathChars != nullptr) {
        environment->ReleaseStringUTFChars(path, pathChars);
    }
}

void android_main(android_app* app) {
    questlab::SetLogTag("RFDetrDetection");
    AndroidState androidState;
    app->userData = &androidState;
    app->onAppCmd = HandleAppCommand;

    const std::filesystem::path privateDirectory =
        app->activity->internalDataPath;
    const questlab::detection::DetectorConfig detectorConfig =
        LoadDetectorConfig(privateDirectory);
    questlab::LogInfo(
        "RF-DETR detection starting backend=%s private_dir=%s",
        BackendName(detectorConfig.kind),
        privateDirectory.c_str());

    const questlab::camera::CameraSourceConfig cameraConfig{
        questlab::camera::CameraSourceKind::MetaCamera2,
        {},
    };
    const questlab::camera::CameraPlatformContext cameraPlatform{
        app->activity->vm,
        app->activity->clazz,
    };
    std::unique_ptr<questlab::camera::IRgbCameraSource> camera =
        questlab::camera::CreateCameraSource(cameraConfig, cameraPlatform);
    auto* metaCamera = dynamic_cast<questlab::camera::MetaCamera2Adapter*>(
        camera.get());
    if (camera == nullptr || metaCamera == nullptr) {
        questlab::LogError("Meta Camera2 source factory failed");
        ANativeActivity_finish(app->activity);
        return;
    }
    gCameraAdapter.store(metaCamera);

    DetectionScene scene(camera.get(), detectorConfig);
    questlab::XrInstanceContext xrInstance;
    questlab::VulkanSessionBinding vulkanBinding;
    questlab::XrSessionContext xrSession;
    questlab::MetaPassthroughFB passthrough;
    questlab::VulkanStereoRenderer renderer;
    questlab::VulkanBindingOptions bindingOptions;
#if defined(QUEST_ENABLE_VULKAN_VALIDATION)
    bindingOptions.enableValidation = true;
#endif
    const questlab::XrInstanceOptions instanceOptions{
        "RF-DETR World Detection",
        1,
        {
            XR_FB_PASSTHROUGH_EXTENSION_NAME,
            XR_META_ENVIRONMENT_DEPTH_EXTENSION_NAME,
        },
    };
    const questlab::VulkanRendererOptions rendererOptions{true};
    bool cameraRequested = false;
    bool detectorRequested = false;

    if (!xrInstance.Initialize(
            app->activity->vm,
            app->activity->clazz,
            instanceOptions) ||
        !vulkanBinding.Initialize(xrInstance, bindingOptions) ||
        !xrSession.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            vulkanBinding.GraphicsBinding()) ||
        xrSession.BlendMode() != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ||
        !passthrough.Initialize(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            xrSession.Session()) ||
        !scene.Initialize(xrInstance.Instance(), xrSession.Session()) ||
        !renderer.Initialize(
            xrInstance.Instance(),
            xrSession,
            vulkanBinding.DeviceContext(),
            &scene,
            rendererOptions)) {
        questlab::LogError("RF-DETR Quest initialization failed");
        ANativeActivity_finish(app->activity);
    } else {
        scene.InitializeDepth(
            xrInstance.Instance(),
            xrInstance.SystemId(),
            xrSession.Session(),
            xrSession.LocalSpace(),
            vulkanBinding.DeviceContext());
        while (!androidState.destroyRequested && !app->destroyRequested &&
               !xrSession.ShouldExit()) {
            const int timeoutMilliseconds =
                xrSession.IsRunning() ? 0 : (androidState.resumed ? 10 : -1);
            int events = 0;
            android_poll_source* source = nullptr;
            int pollResult = ALooper_pollOnce(
                timeoutMilliseconds,
                nullptr,
                &events,
                reinterpret_cast<void**>(&source));
            while (pollResult >= 0) {
                if (source != nullptr) {
                    source->process(app, source);
                }
                if (androidState.destroyRequested || app->destroyRequested) {
                    break;
                }
                source = nullptr;
                pollResult = ALooper_pollOnce(
                    0,
                    nullptr,
                    &events,
                    reinterpret_cast<void**>(&source));
            }
            if (androidState.destroyRequested || app->destroyRequested) {
                xrSession.RequestExit();
                break;
            }
            if (androidState.resumed && !cameraRequested) {
                camera->Start({0, 0, 30});
                cameraRequested = true;
            } else if (!androidState.resumed && cameraRequested) {
                camera->Stop();
                cameraRequested = false;
            }
            if (androidState.resumed && !detectorRequested) {
                scene.StartDetector();
                detectorRequested = true;
            } else if (!androidState.resumed && detectorRequested) {
                scene.StopDetector();
                detectorRequested = false;
            }
            if (!xrSession.PollEvents(&passthrough) ||
                !passthrough.SetActive(
                    androidState.resumed && xrSession.IsRunning()) ||
                !xrSession.PumpFrame(&renderer, &scene, &passthrough)) {
                break;
            }
        }
    }

    gCameraAdapter.store(nullptr);
    camera->Stop();
    scene.Shutdown();
    if (!androidState.destroyRequested && !app->destroyRequested) {
        ANativeActivity_finish(app->activity);
    }
    renderer.Shutdown();
    passthrough.Shutdown();
    xrSession.Shutdown();
    vulkanBinding.Shutdown();
    xrInstance.Shutdown();
    questlab::LogInfo("RF-DETR detection stopped cleanly");
}
