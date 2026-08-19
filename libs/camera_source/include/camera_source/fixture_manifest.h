#pragma once

#include "camera_source/camera_source.h"

#include <filesystem>
#include <string>

namespace questlab::camera {

constexpr const char* kQuestCameraFixtureVersion =
    "QUEST_CAMERA_FIXTURE_V2";

// Hashes the fixture geometry and the exact owned Y, U, and V plane byte
// payloads. Padding bytes are deliberately included so a fixture is immutable
// byte-for-byte, while colour conversion remains defined only by
// ConvertYuv420ToRgba.
std::string ComputeQuestCameraPixelSha256(const RgbCapture& capture);

bool LoadQuestCameraFixture(
    const std::filesystem::path& manifestPath,
    RgbCapture* capture,
    std::string* error);

}  // namespace questlab::camera
