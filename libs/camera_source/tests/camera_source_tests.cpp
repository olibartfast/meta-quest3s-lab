#include "camera_source/fixture_manifest.h"
#include "camera_source/latest_frame_queue.h"
#include "camera_source/yuv_converter.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

questlab::camera::RgbCapture MakeWhiteFixtureCapture() {
    questlab::camera::RgbCapture capture;
    capture.width = 2;
    capture.height = 2;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0].bytes = {235, 235, 235, 235};
    capture.planes[0].rowStride = 2;
    capture.planes[0].pixelStride = 1;
    capture.planes[1].bytes = {128};
    capture.planes[1].rowStride = 1;
    capture.planes[1].pixelStride = 1;
    capture.planes[2].bytes = {128};
    capture.planes[2].rowStride = 1;
    capture.planes[2].pixelStride = 1;
    return capture;
}

void WriteFixture(
    const std::filesystem::path& directory,
    const std::string& version,
    const std::string& checksum) {
    const questlab::camera::RgbCapture capture = MakeWhiteFixtureCapture();
    std::ofstream(directory / "y.bin", std::ios::binary).write(
        reinterpret_cast<const char*>(capture.planes[0].bytes.data()),
        static_cast<std::streamsize>(capture.planes[0].bytes.size()));
    std::ofstream(directory / "u.bin", std::ios::binary).write(
        reinterpret_cast<const char*>(capture.planes[1].bytes.data()),
        static_cast<std::streamsize>(capture.planes[1].bytes.size()));
    std::ofstream(directory / "v.bin", std::ios::binary).write(
        reinterpret_cast<const char*>(capture.planes[2].bytes.data()),
        static_cast<std::streamsize>(capture.planes[2].bytes.size()));
    std::ofstream manifest(directory / "manifest.qcam");
    manifest << version << " 2 2 2 1 1 1 1 1 y.bin u.bin v.bin\n";
    if (!checksum.empty()) {
        manifest << "pixel_sha256 " << checksum << '\n';
    }
    manifest
        << "sensor_timestamp_ns 1234\n"
        << "intrinsics 100 101 1 1 0\n"
        << "distortion 0 0 0 0 0\n"
        << "camera_from_head 0 0 0 1 0.01 0.02 0.03\n";
}

void TestLatestFrameWins() {
    questlab::camera::LatestFrameQueue queue;
    questlab::camera::RgbCapture first;
    first.frameId = 1;
    questlab::camera::RgbCapture second;
    second.frameId = 2;
    assert(!queue.Publish(std::move(first)));
    assert(queue.Publish(std::move(second)));
    questlab::camera::RgbCapture result;
    assert(queue.TryConsumeLatest(&result));
    assert(result.frameId == 2);
    assert(!queue.TryConsumeLatest(&result));
}

void TestStrideAwareYuvConversion() {
    questlab::camera::RgbCapture capture;
    capture.width = 2;
    capture.height = 2;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0].bytes = {235, 235, 0, 0, 235, 235};
    capture.planes[0].rowStride = 4;
    capture.planes[0].pixelStride = 1;
    capture.planes[1].bytes = {128, 0};
    capture.planes[1].rowStride = 2;
    capture.planes[1].pixelStride = 1;
    capture.planes[2].bytes = {128, 0};
    capture.planes[2].rowStride = 2;
    capture.planes[2].pixelStride = 1;
    std::vector<uint8_t> rgba;
    assert(questlab::camera::ConvertYuv420ToRgba(capture, &rgba));
    assert(rgba.size() == 16);
    for (size_t offset = 0; offset < rgba.size(); offset += 4) {
        assert(rgba[offset] >= 250);
        assert(rgba[offset + 1] >= 250);
        assert(rgba[offset + 2] >= 250);
        assert(rgba[offset + 3] == 255);
    }
}

void TestStrideAwareRgbaCopyForStreaming() {
    questlab::camera::RgbCapture capture;
    capture.width = 2;
    capture.height = 2;
    capture.format = questlab::camera::PixelFormat::Rgba8888;
    capture.planes[0].bytes = {
        1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0,
        9, 10, 11, 12, 13, 14, 15, 16, 0, 0, 0, 0,
    };
    capture.planes[0].rowStride = 12;
    capture.planes[0].pixelStride = 4;
    std::vector<uint8_t> rgba;
    assert(questlab::camera::ConvertYuv420ToRgba(capture, &rgba));
    assert(rgba == std::vector<uint8_t>({
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16,
    }));
}

void TestReplayUsesPortableFactoryContract() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "questlab-camera-source-test";
    std::filesystem::create_directories(directory);
    const questlab::camera::RgbCapture fixture = MakeWhiteFixtureCapture();
    WriteFixture(
        directory,
        questlab::camera::kQuestCameraFixtureVersion,
        questlab::camera::ComputeQuestCameraPixelSha256(fixture));
    const questlab::camera::CameraSourceConfig config{
        questlab::camera::CameraSourceKind::Replay,
        (directory / "manifest.qcam").string(),
    };
    std::unique_ptr<questlab::camera::IRgbCameraSource> source =
        questlab::camera::CreateCameraSource(config, {});
    assert(source != nullptr);
    assert(source->Start({2, 2, 30}));
    questlab::camera::RgbCapture capture;
    assert(source->TryConsumeLatest(&capture));
    assert(capture.width == 2);
    assert(capture.height == 2);
    assert(capture.intrinsics.valid);
    assert(capture.cameraFromHead.valid);
    std::vector<uint8_t> rgba;
    assert(questlab::camera::ConvertYuv420ToRgba(capture, &rgba));
    source->Stop();
    std::filesystem::remove_all(directory);
}

void TestReplayRejectsUnknownVersionAndCorruptPixels() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "questlab-camera-source-integrity-test";
    std::filesystem::create_directories(directory);
    const questlab::camera::RgbCapture fixture = MakeWhiteFixtureCapture();
    const std::string checksum =
        questlab::camera::ComputeQuestCameraPixelSha256(fixture);

    WriteFixture(directory, "QUEST_CAMERA_FIXTURE_V1", checksum);
    questlab::camera::RgbCapture loaded;
    std::string error;
    assert(!questlab::camera::LoadQuestCameraFixture(
        directory / "manifest.qcam", &loaded, &error));
    assert(error.find("unsupported") != std::string::npos);

    WriteFixture(
        directory,
        questlab::camera::kQuestCameraFixtureVersion,
        checksum);
    std::ofstream(directory / "y.bin", std::ios::binary | std::ios::trunc)
        .write("\x10\xeb\xeb\xeb", 4);
    error.clear();
    assert(!questlab::camera::LoadQuestCameraFixture(
        directory / "manifest.qcam", &loaded, &error));
    assert(error.find("checksum mismatch") != std::string::npos);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    TestLatestFrameWins();
    TestStrideAwareYuvConversion();
    TestStrideAwareRgbaCopyForStreaming();
    TestReplayUsesPortableFactoryContract();
    TestReplayRejectsUnknownVersionAndCorruptPixels();
    return 0;
}
