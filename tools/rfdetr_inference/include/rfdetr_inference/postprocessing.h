#pragma once

#include "rfdetr_inference/model_contract.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace questlab::rfdetr {

struct Detection {
    int32_t classId = -1;
    std::string className;
    float confidence = 0.0F;
    std::array<float, 4> boxXyxy{};
};

struct ComparisonResult {
    bool matches = false;
    std::string message;
};

bool PostprocessDetections(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error);

float BoxIou(
    const std::array<float, 4>& first,
    const std::array<float, 4>& second);

ComparisonResult CompareDetections(
    const std::vector<Detection>& expected,
    const std::vector<Detection>& actual,
    const ModelContract& contract);

}  // namespace questlab::rfdetr
