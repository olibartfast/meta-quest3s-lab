#include "object_detector/object_detector.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char* kIdentity =
    "1ac37bb3429380c7aee6b2c2778d20026db0ad18579cac119255d31f736dc760";

std::filesystem::path WriteReplayFile() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "questlab-object-detector-replay.json";
    std::ofstream output(path);
    output << R"({
  "schema": "QUESTLAB_RFDETR_DETECTIONS_V1",
  "model": {"onnx_sha256": ")" << kIdentity << R"("},
  "detections": [
    {"class_id": 1, "class_name": "person", "confidence": 0.875,
     "box_xyxy": [10, 20, 110, 220]}
  ]
})";
    output.close();
    return path;
}

questlab::camera::RgbCapture Capture(uint64_t frameId) {
    questlab::camera::RgbCapture capture;
    capture.frameId = frameId;
    capture.sensorTimestampNanoseconds = 1234;
    capture.width = 320;
    capture.height = 240;
    return capture;
}

}  // namespace

int main() {
    using namespace questlab::detection;
    DetectorConfig config;
    config.kind = ObjectDetectorKind::Replay;
    config.replayDetectionsPath = WriteReplayFile().string();
    config.expectedManifestIdentity = kIdentity;
    config.replayFrameId = 42;

    std::unique_ptr<IObjectDetector> detector = CreateObjectDetector(config);
    assert(detector != nullptr);
    assert(detector->GetCapabilities().kind == ObjectDetectorKind::Replay);
    assert(detector->Start(config));
    assert(detector->GetStats().health == DetectorHealth::Running);

    assert(!detector->Submit(Capture(41)));
    assert(detector->GetStats().unknownReplayFrames == 1);
    assert(detector->Submit(Capture(42)));
    DetectionResult first;
    assert(detector->TryConsumeLatest(&first));
    assert(first.frameId == 42);
    assert(first.manifestIdentity == kIdentity);
    assert(first.detections.size() == 1);
    assert(first.detections[0].className == "person");

    assert(detector->Submit(Capture(42)));
    DetectionResult second;
    assert(detector->TryConsumeLatest(&second));
    assert(second.detections[0].boxXyxy == first.detections[0].boxXyxy);
    detector->Stop();
    assert(detector->GetStats().health == DetectorHealth::Stopped);

    config.expectedManifestIdentity = std::string(64, '0');
    detector = CreateObjectDetector(config);
    assert(!detector->Start(config));
    assert(detector->GetStats().health == DetectorHealth::Error);

    config.kind = ObjectDetectorKind::OnDeviceOnnxRuntime;
    detector = CreateObjectDetector(config);
    assert(detector->GetCapabilities().kind ==
           ObjectDetectorKind::OnDeviceOnnxRuntime);
    config.kind = ObjectDetectorKind::Streaming;
    detector = CreateObjectDetector(config);
    assert(detector->GetCapabilities().kind == ObjectDetectorKind::Streaming);

    std::filesystem::remove(WriteReplayFile());
    return 0;
}
