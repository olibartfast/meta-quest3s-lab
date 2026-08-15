#include "object_detector/object_detector.h"

#include "camera_source/yuv_converter.h"
#include "rfdetr_inference/detection_io.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/onnx_session.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace questlab::detection {
namespace {

constexpr size_t kManifestIdentityBytes = 64;
constexpr size_t kMaximumWireDetections = 300;

int64_t SteadyNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<Detection> ConvertDetections(
    const std::vector<rfdetr::Detection>& source) {
    std::vector<Detection> converted;
    converted.reserve(source.size());
    for (const rfdetr::Detection& detection : source) {
        converted.push_back({
            detection.classId,
            detection.className,
            detection.confidence,
            detection.boxXyxy,
        });
    }
    return converted;
}

class AsyncNewestFrameDetector : public IObjectDetector {
public:
    ~AsyncNewestFrameDetector() override {
        Stop();
    }

    bool Start(const DetectorConfig& config) final {
        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_ = config;
            stats_ = {};
            stats_.health = DetectorHealth::Starting;
            stopping_ = false;
            lastAcceptedNanoseconds_ = 0;
        }
        worker_ = std::thread(&AsyncNewestFrameDetector::WorkerMain, this);
        return true;
    }

    bool Submit(camera::RgbCapture capture) final {
        const int64_t now = SteadyNanoseconds();
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || (stats_.health != DetectorHealth::Starting &&
                          stats_.health != DetectorHealth::Running)) {
            return false;
        }
        if (config_.maximumSubmissionsPerSecond > 0.0F &&
            lastAcceptedNanoseconds_ > 0) {
            const double minimumInterval =
                1.0e9 / config_.maximumSubmissionsPerSecond;
            if (static_cast<double>(now - lastAcceptedNanoseconds_) <
                minimumInterval) {
                ++stats_.rateLimitedFrames;
                return false;
            }
        }
        lastAcceptedNanoseconds_ = now;
        if (pending_.has_value()) {
            ++stats_.replacedPendingFrames;
        }
        pending_ = std::move(capture);
        ++stats_.submittedFrames;
        stats_.currentQueueDepth = 1;
        stats_.queueHighWaterMark =
            std::max<uint64_t>(stats_.queueHighWaterMark, 1);
        condition_.notify_one();
        return true;
    }

    bool TryConsumeLatest(DetectionResult* result) final {
        if (result == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!latestResult_.has_value()) {
            return false;
        }
        *result = std::move(*latestResult_);
        latestResult_.reset();
        return true;
    }

    DetectorStats GetStats() const final {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void Stop() final {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!worker_.joinable()) {
                stats_.health = DetectorHealth::Stopped;
                return;
            }
            stopping_ = true;
            pending_.reset();
            stats_.currentQueueDepth = 0;
        }
        CancelBackend();
        condition_.notify_all();
        worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.health = DetectorHealth::Stopped;
        latestResult_.reset();
    }

protected:
    virtual bool Prepare(
        const DetectorConfig& config,
        std::string* manifestIdentity,
        std::string* error) = 0;
    virtual bool Process(
        camera::RgbCapture capture,
        const std::string& manifestIdentity,
        DetectionResult* result,
        std::string* error) = 0;
    virtual void CancelBackend() {}
    virtual DetectorHealth FailureHealth() const {
        return DetectorHealth::Error;
    }
    virtual std::string BackendDetails() const { return {}; }

private:
    void WorkerMain() {
        DetectorConfig config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
        }
        std::string manifestIdentity;
        std::string error;
        if (!Prepare(config, &manifestIdentity, &error)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_) {
                stats_.health = FailureHealth();
                stats_.lastError = error;
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
            stats_.manifestIdentity = manifestIdentity;
            stats_.backendDetails = BackendDetails();
            stats_.health = DetectorHealth::Running;
        }

        while (true) {
            camera::RgbCapture capture;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [&]() {
                    return stopping_ || pending_.has_value();
                });
                if (stopping_) {
                    return;
                }
                capture = std::move(*pending_);
                pending_.reset();
                stats_.currentQueueDepth = 0;
            }

            DetectionResult result;
            error.clear();
            if (Process(
                    std::move(capture),
                    manifestIdentity,
                    &result,
                    &error)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) {
                    latestResult_ = std::move(result);
                    ++stats_.completedFrames;
                    stats_.lastCompletedFrameId = latestResult_->frameId;
                }
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_) {
                    ++stats_.inferenceFailures;
                    stats_.lastError = error;
                    stats_.health = FailureHealth();
                    return;
                }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    DetectorConfig config_;
    DetectorStats stats_;
    std::optional<camera::RgbCapture> pending_;
    std::optional<DetectionResult> latestResult_;
    std::thread worker_;
    bool stopping_ = true;
    int64_t lastAcceptedNanoseconds_ = 0;
};

class OnDeviceOnnxRuntimeDetector final :
    public AsyncNewestFrameDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {
            ObjectDetectorKind::OnDeviceOnnxRuntime,
            "ONNX Runtime XNNPACK with CPU fallback",
            true,
            true,
            true,
        };
    }

protected:
    bool Prepare(
        const DetectorConfig& config,
        std::string* manifestIdentity,
        std::string* error) override {
        if (config.modelPath.empty() || config.manifestPath.empty()) {
            if (error != nullptr) {
                *error = "On-device model or manifest path is empty";
            }
            return false;
        }
        if (!rfdetr::LoadModelContract(
                config.manifestPath, &contract_, error)) {
            return false;
        }
        if (!config.expectedManifestIdentity.empty() &&
            config.expectedManifestIdentity != contract_.onnxSha256) {
            if (error != nullptr) {
                *error = "Configured manifest identity does not match model manifest";
            }
            return false;
        }
        session_ = std::make_unique<rfdetr::OnnxSession>();
        rfdetr::RuntimeOptions options;
#if defined(__ANDROID__)
        options.executionProvider = "XNNPACK";
        options.xnnpackThreads = std::max(config.xnnpackThreads, 1);
#else
        options.executionProvider = "CPU";
#endif
        if (!session_->Initialize(
                config.modelPath, contract_, options, error)) {
            return false;
        }

        const size_t inputCount = std::accumulate(
            contract_.input.shape.begin(),
            contract_.input.shape.end(),
            size_t{1},
            [](size_t product, int64_t dimension) {
                return product * static_cast<size_t>(dimension);
            });
        std::vector<float> warmupInput(inputCount, 0.0F);
        std::vector<float> boxes;
        std::vector<float> logits;
        if (!session_->Run(warmupInput, &boxes, &logits, error)) {
            return false;
        }
        *manifestIdentity = contract_.onnxSha256;
        return true;
    }

    bool Process(
        camera::RgbCapture capture,
        const std::string& manifestIdentity,
        DetectionResult* result,
        std::string* error) override {
        if (result == nullptr || session_ == nullptr) {
            if (error != nullptr) {
                *error = "On-device detector is not initialized";
            }
            return false;
        }
        DetectionResult completed;
        completed.frameId = capture.frameId;
        completed.captureTimestampNanoseconds =
            capture.sensorTimestampNanoseconds;
        completed.sourceWidth = capture.width;
        completed.sourceHeight = capture.height;
        completed.manifestIdentity = manifestIdentity;
        completed.inferenceStartNanoseconds = SteadyNanoseconds();

        std::vector<uint8_t> rgba;
        std::vector<float> input;
        if (!rfdetr::PreprocessCapture(
                capture, contract_, &rgba, &input, error)) {
            return false;
        }
        std::vector<float> boxes;
        std::vector<float> logits;
        if (!session_->Run(input, &boxes, &logits, error)) {
            return false;
        }
        std::vector<rfdetr::Detection> detections;
        if (!rfdetr::PostprocessDetections(
                boxes,
                logits,
                capture.width,
                capture.height,
                contract_,
                &detections,
                error)) {
            return false;
        }
        completed.inferenceEndNanoseconds = SteadyNanoseconds();
        completed.detections = ConvertDetections(detections);
        *result = std::move(completed);
        return true;
    }

    void CancelBackend() override {
        if (session_ != nullptr) {
            session_->RequestCancellation();
        }
    }

    std::string BackendDetails() const override {
        return session_ != nullptr ? session_->RuntimeDescription() : "";
    }

private:
    rfdetr::ModelContract contract_;
    std::unique_ptr<rfdetr::OnnxSession> session_;
};

#if !defined(_WIN32)

uint64_t HostToNetwork64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32U));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32U) | high;
}

uint64_t NetworkToHost64(uint64_t value) {
    const uint32_t high = ntohl(static_cast<uint32_t>(value));
    const uint32_t low = ntohl(static_cast<uint32_t>(value >> 32U));
    return (static_cast<uint64_t>(high) << 32U) | low;
}

bool SendAll(int socket, const void* bytes, size_t size) {
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t sent = send(socket, cursor, size, 0);
        if (sent <= 0) {
            return false;
        }
        cursor += static_cast<size_t>(sent);
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(int socket, void* bytes, size_t size) {
    auto* cursor = static_cast<uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t received = recv(socket, cursor, size, 0);
        if (received <= 0) {
            return false;
        }
        cursor += static_cast<size_t>(received);
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool SendU32(int socket, uint32_t value) {
    value = htonl(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU32(int socket, uint32_t* value) {
    uint32_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) {
        return false;
    }
    *value = ntohl(wire);
    return true;
}

bool SendU64(int socket, uint64_t value) {
    value = HostToNetwork64(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU64(int socket, uint64_t* value) {
    uint64_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) {
        return false;
    }
    *value = NetworkToHost64(wire);
    return true;
}

bool ReceiveFloat(int socket, float* value) {
    uint32_t wire = 0;
    if (!ReceiveU32(socket, &wire)) {
        return false;
    }
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(value, &wire, sizeof(*value));
    return true;
}

class StreamingDetector final : public AsyncNewestFrameDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {
            ObjectDetectorKind::Streaming,
            "Trusted-link RF-DETR service v1",
            true,
            true,
            true,
        };
    }

protected:
    bool Prepare(
        const DetectorConfig& config,
        std::string* manifestIdentity,
        std::string* error) override {
        if (config.expectedManifestIdentity.size() !=
            kManifestIdentityBytes) {
            if (error != nullptr) {
                *error = "Streaming requires a 64-character manifest identity";
            }
            return false;
        }
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* addresses = nullptr;
        const std::string port = std::to_string(config.servicePort);
        if (getaddrinfo(
                config.serviceHost.c_str(),
                port.c_str(),
                &hints,
                &addresses) != 0) {
            if (error != nullptr) {
                *error = "Cannot resolve streaming service host";
            }
            return false;
        }
        int connected = -1;
        for (addrinfo* address = addresses;
             address != nullptr;
             address = address->ai_next) {
            connected = socket(
                address->ai_family,
                address->ai_socktype,
                address->ai_protocol);
            if (connected >= 0 &&
                connect(connected, address->ai_addr, address->ai_addrlen) == 0) {
                break;
            }
            if (connected >= 0) {
                close(connected);
                connected = -1;
            }
        }
        freeaddrinfo(addresses);
        if (connected < 0) {
            if (error != nullptr) {
                *error = "Cannot connect to streaming service";
            }
            return false;
        }
        socket_.store(connected);
        const char magic[4] = {'Q', 'L', 'D', '1'};
        std::array<char, kManifestIdentityBytes> identity{};
        std::copy(
            config.expectedManifestIdentity.begin(),
            config.expectedManifestIdentity.end(),
            identity.begin());
        std::array<char, 4> replyMagic{};
        std::array<char, kManifestIdentityBytes> replyIdentity{};
        if (!SendAll(connected, magic, sizeof(magic)) ||
            !SendAll(connected, identity.data(), identity.size()) ||
            !ReceiveAll(connected, replyMagic.data(), replyMagic.size()) ||
            !ReceiveAll(
                connected, replyIdentity.data(), replyIdentity.size()) ||
            replyMagic != std::array<char, 4>{'Q', 'L', 'D', '1'} ||
            replyIdentity != identity) {
            if (error != nullptr) {
                *error = "Streaming handshake or manifest identity mismatch";
            }
            close(connected);
            socket_.store(-1);
            return false;
        }
        *manifestIdentity = config.expectedManifestIdentity;
        return true;
    }

    bool Process(
        camera::RgbCapture capture,
        const std::string& manifestIdentity,
        DetectionResult* result,
        std::string* error) override {
        const int connected = socket_.load();
        if (connected < 0 || result == nullptr) {
            if (error != nullptr) {
                *error = "Streaming service is disconnected";
            }
            return false;
        }
        std::vector<uint8_t> rgba;
        if (!camera::ConvertYuv420ToRgba(capture, &rgba)) {
            if (error != nullptr) {
                *error = "Streaming frame conversion failed";
            }
            return false;
        }
        if (rgba.size() > std::numeric_limits<uint32_t>::max()) {
            if (error != nullptr) {
                *error = "Streaming frame is too large";
            }
            return false;
        }
        const char frameMagic[4] = {'F', 'R', 'M', '1'};
        if (!SendAll(connected, frameMagic, sizeof(frameMagic)) ||
            !SendU64(connected, capture.frameId) ||
            !SendU64(
                connected,
                static_cast<uint64_t>(capture.sensorTimestampNanoseconds)) ||
            !SendU32(connected, static_cast<uint32_t>(capture.width)) ||
            !SendU32(connected, static_cast<uint32_t>(capture.height)) ||
            !SendU32(connected, static_cast<uint32_t>(rgba.size())) ||
            !SendAll(connected, rgba.data(), rgba.size())) {
            if (error != nullptr) {
                *error = "Streaming frame send failed";
            }
            return false;
        }

        std::array<char, 4> responseMagic{};
        uint64_t responseFrameId = 0;
        uint64_t inferenceStart = 0;
        uint64_t inferenceEnd = 0;
        uint32_t detectionCount = 0;
        if (!ReceiveAll(
                connected, responseMagic.data(), responseMagic.size()) ||
            responseMagic != std::array<char, 4>{'D', 'E', 'T', '1'} ||
            !ReceiveU64(connected, &responseFrameId) ||
            !ReceiveU64(connected, &inferenceStart) ||
            !ReceiveU64(connected, &inferenceEnd) ||
            !ReceiveU32(connected, &detectionCount) ||
            detectionCount > kMaximumWireDetections) {
            if (error != nullptr) {
                *error = "Streaming response header is invalid";
            }
            return false;
        }
        DetectionResult completed;
        completed.frameId = responseFrameId;
        completed.captureTimestampNanoseconds =
            capture.sensorTimestampNanoseconds;
        completed.inferenceStartNanoseconds =
            static_cast<int64_t>(inferenceStart);
        completed.inferenceEndNanoseconds =
            static_cast<int64_t>(inferenceEnd);
        completed.sourceWidth = capture.width;
        completed.sourceHeight = capture.height;
        completed.manifestIdentity = manifestIdentity;
        completed.detections.reserve(detectionCount);
        for (uint32_t index = 0; index < detectionCount; ++index) {
            uint32_t classId = 0;
            uint32_t nameLength = 0;
            Detection detection;
            if (!ReceiveU32(connected, &classId) ||
                !ReceiveFloat(connected, &detection.confidence)) {
                if (error != nullptr) {
                    *error = "Streaming detection payload is truncated";
                }
                return false;
            }
            detection.classId = static_cast<int32_t>(classId);
            for (float& coordinate : detection.boxXyxy) {
                if (!ReceiveFloat(connected, &coordinate)) {
                    if (error != nullptr) {
                        *error = "Streaming box payload is truncated";
                    }
                    return false;
                }
            }
            if (!ReceiveU32(connected, &nameLength) || nameLength > 128U) {
                if (error != nullptr) {
                    *error = "Streaming label length is invalid";
                }
                return false;
            }
            detection.className.resize(nameLength);
            if (!ReceiveAll(
                    connected,
                    detection.className.data(),
                    detection.className.size())) {
                if (error != nullptr) {
                    *error = "Streaming label payload is truncated";
                }
                return false;
            }
            completed.detections.push_back(std::move(detection));
        }
        *result = std::move(completed);
        return true;
    }

    void CancelBackend() override {
        const int connected = socket_.exchange(-1);
        if (connected >= 0) {
            shutdown(connected, SHUT_RDWR);
            close(connected);
        }
    }

    DetectorHealth FailureHealth() const override {
        return DetectorHealth::Disconnected;
    }

private:
    std::atomic<int> socket_{-1};
};

#else

class StreamingDetector final : public IObjectDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {ObjectDetectorKind::Streaming, "Unavailable on Windows"};
    }
    bool Start(const DetectorConfig&) override { return false; }
    bool Submit(camera::RgbCapture) override { return false; }
    bool TryConsumeLatest(DetectionResult*) override { return false; }
    DetectorStats GetStats() const override {
        DetectorStats stats;
        stats.health = DetectorHealth::Unsupported;
        stats.lastError = "Streaming backend is unavailable on Windows";
        return stats;
    }
    void Stop() override {}
};

#endif

class ReplayDetector final : public IObjectDetector {
public:
    DetectorCapabilities GetCapabilities() const override {
        return {
            ObjectDetectorKind::Replay,
            "Pinned reference detection replay",
            true,
            true,
            true,
        };
    }

    bool Start(const DetectorConfig& config) override {
        Stop();
        std::vector<rfdetr::Detection> loaded;
        std::string identity;
        std::string error;
        if (config.replayDetectionsPath.empty() ||
            !rfdetr::LoadDetectionJson(
                config.replayDetectionsPath,
                &loaded,
                &identity,
                &error)) {
            stats_.health = DetectorHealth::Error;
            stats_.lastError = error.empty()
                ? "Replay detection path is empty"
                : error;
            return false;
        }
        if (!config.expectedManifestIdentity.empty() &&
            config.expectedManifestIdentity != identity) {
            stats_.health = DetectorHealth::Error;
            stats_.lastError =
                "Replay manifest identity does not match configuration";
            return false;
        }
        config_ = config;
        replayDetections_ = ConvertDetections(loaded);
        stats_ = {};
        stats_.health = DetectorHealth::Running;
        stats_.manifestIdentity = identity;
        return true;
    }

    bool Submit(camera::RgbCapture capture) override {
        if (stats_.health != DetectorHealth::Running) {
            return false;
        }
        ++stats_.submittedFrames;
        if (config_.replayFrameId != 0 &&
            capture.frameId != config_.replayFrameId) {
            ++stats_.unknownReplayFrames;
            return false;
        }
        DetectionResult result;
        result.frameId = capture.frameId;
        result.captureTimestampNanoseconds = capture.sensorTimestampNanoseconds;
        result.inferenceStartNanoseconds = SteadyNanoseconds();
        result.inferenceEndNanoseconds = result.inferenceStartNanoseconds;
        result.sourceWidth = capture.width;
        result.sourceHeight = capture.height;
        result.manifestIdentity = stats_.manifestIdentity;
        result.detections = replayDetections_;
        latestResult_ = std::move(result);
        ++stats_.completedFrames;
        stats_.lastCompletedFrameId = capture.frameId;
        return true;
    }

    bool TryConsumeLatest(DetectionResult* result) override {
        if (result == nullptr || !latestResult_.has_value()) {
            return false;
        }
        *result = std::move(*latestResult_);
        latestResult_.reset();
        return true;
    }

    DetectorStats GetStats() const override { return stats_; }

    void Stop() override {
        replayDetections_.clear();
        latestResult_.reset();
        stats_.health = DetectorHealth::Stopped;
    }

private:
    DetectorConfig config_;
    DetectorStats stats_;
    std::vector<Detection> replayDetections_;
    std::optional<DetectionResult> latestResult_;
};

}  // namespace

std::unique_ptr<IObjectDetector> CreateObjectDetector(
    const DetectorConfig& config) {
    switch (config.kind) {
        case ObjectDetectorKind::OnDeviceOnnxRuntime:
            return std::make_unique<OnDeviceOnnxRuntimeDetector>();
        case ObjectDetectorKind::Streaming:
            return std::make_unique<StreamingDetector>();
        case ObjectDetectorKind::Replay:
            return std::make_unique<ReplayDetector>();
    }
    return nullptr;
}

bool AnnotateRgba(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<Detection>& detections,
    const std::vector<std::string>& statusLines,
    std::string* error) {
    std::vector<rfdetr::Detection> converted;
    converted.reserve(detections.size());
    for (const Detection& detection : detections) {
        converted.push_back({
            detection.classId,
            detection.className,
            detection.confidence,
            detection.boxXyxy,
        });
    }
    return rfdetr::AnnotateRgba(
               width, height, rgba, converted, error) &&
           rfdetr::AnnotateRgbaStatus(
               width, height, rgba, statusLines, error);
}

}  // namespace questlab::detection
