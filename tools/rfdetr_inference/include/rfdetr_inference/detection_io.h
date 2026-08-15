#pragma once

#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/timing.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace questlab::rfdetr {

bool AnnotateRgba(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<Detection>& detections,
    std::string* error);

bool AnnotateRgbaStatus(
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* rgba,
    const std::vector<std::string>& statusLines,
    std::string* error);

bool WriteRawRgba(
    const std::filesystem::path& path,
    const std::vector<uint8_t>& rgba,
    std::string* error);

bool WriteAnnotatedPpm(
    const std::filesystem::path& path,
    int32_t width,
    int32_t height,
    const std::vector<uint8_t>& rgba,
    const std::vector<Detection>& detections,
    std::string* error);

bool WriteDetectionJson(
    const std::filesystem::path& path,
    int32_t width,
    int32_t height,
    const std::string& fixturePixelSha256,
    const std::vector<Detection>& detections,
    const TimingSummary& preprocessing,
    const TimingSummary& inference,
    const TimingSummary& postprocessing,
    const ModelContract& contract,
    std::string* error);

bool LoadDetectionJson(
    const std::filesystem::path& path,
    std::vector<Detection>* detections,
    std::string* error);

bool LoadDetectionJson(
    const std::filesystem::path& path,
    std::vector<Detection>* detections,
    std::string* manifestIdentity,
    std::string* error);

}  // namespace questlab::rfdetr
