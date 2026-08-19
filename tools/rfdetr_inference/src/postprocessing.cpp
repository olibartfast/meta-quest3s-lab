#include "rfdetr_inference/postprocessing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace questlab::rfdetr {
namespace {

struct Candidate {
    float score = 0.0F;
    size_t flatIndex = 0;
};

float Sigmoid(float value) {
    if (value >= 0.0F) {
        return 1.0F / (1.0F + std::exp(-value));
    }
    const float exponential = std::exp(value);
    return exponential / (1.0F + exponential);
}

}  // namespace

bool PostprocessDetections(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error) {
    if (detections == nullptr) {
        if (error != nullptr) {
            *error = "Detection output pointer is null";
        }
        return false;
    }
    detections->clear();
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        contract.outputs.size() != 2U ||
        contract.outputs[0].shape.size() != 3U ||
        contract.outputs[1].shape.size() != 3U) {
        if (error != nullptr) {
            *error = "Invalid source or output contract dimensions";
        }
        return false;
    }
    const size_t queryCount =
        static_cast<size_t>(contract.outputs[0].shape[1]);
    const size_t classCount =
        static_cast<size_t>(contract.outputs[1].shape[2]);
    if (normalizedCxcywh.size() != queryCount * 4U ||
        classLogits.size() != queryCount * classCount) {
        if (error != nullptr) {
            *error = "RF-DETR output tensor size does not match the manifest";
        }
        return false;
    }
    for (float value : normalizedCxcywh) {
        if (!std::isfinite(value)) {
            if (error != nullptr) {
                *error = "RF-DETR box output contains NaN or infinity";
            }
            return false;
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(classLogits.size());
    for (size_t index = 0; index < classLogits.size(); ++index) {
        if (!std::isfinite(classLogits[index])) {
            if (error != nullptr) {
                *error = "RF-DETR label output contains NaN or infinity";
            }
            return false;
        }
        candidates.push_back({Sigmoid(classLogits[index]), index});
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& first, const Candidate& second) {
            if (first.score != second.score) {
                return first.score > second.score;
            }
            return first.flatIndex < second.flatIndex;
        });
    const size_t selectedCount = std::min(
        static_cast<size_t>(contract.maximumDetections), candidates.size());
    detections->reserve(selectedCount);
    for (size_t index = 0; index < selectedCount; ++index) {
        const Candidate& candidate = candidates[index];
        if (candidate.score <= contract.confidenceThreshold) {
            continue;
        }
        const size_t queryIndex = candidate.flatIndex / classCount;
        const int32_t classId =
            static_cast<int32_t>(candidate.flatIndex % classCount);
        const auto className = contract.classNames.find(classId);
        if (className == contract.classNames.end()) {
            if (error != nullptr) {
                *error = "Selected RF-DETR logit has no COCO sparse label: " +
                         std::to_string(classId);
            }
            return false;
        }
        const size_t boxOffset = queryIndex * 4U;
        const float centerX = normalizedCxcywh[boxOffset];
        const float centerY = normalizedCxcywh[boxOffset + 1U];
        const float width = normalizedCxcywh[boxOffset + 2U];
        const float height = normalizedCxcywh[boxOffset + 3U];
        if (width < 0.0F || height < 0.0F) {
            if (error != nullptr) {
                *error = "RF-DETR box output contains a negative extent";
            }
            return false;
        }
        Detection detection;
        detection.classId = classId;
        detection.className = className->second;
        detection.confidence = candidate.score;
        detection.boxXyxy = {
            std::clamp(
                (centerX - width * 0.5F) * static_cast<float>(sourceWidth),
                0.0F,
                static_cast<float>(sourceWidth)),
            std::clamp(
                (centerY - height * 0.5F) * static_cast<float>(sourceHeight),
                0.0F,
                static_cast<float>(sourceHeight)),
            std::clamp(
                (centerX + width * 0.5F) * static_cast<float>(sourceWidth),
                0.0F,
                static_cast<float>(sourceWidth)),
            std::clamp(
                (centerY + height * 0.5F) * static_cast<float>(sourceHeight),
                0.0F,
                static_cast<float>(sourceHeight)),
        };
        if (detection.boxXyxy[2] < detection.boxXyxy[0] ||
            detection.boxXyxy[3] < detection.boxXyxy[1]) {
            if (error != nullptr) {
                *error = "RF-DETR box becomes inverted after source mapping";
            }
            return false;
        }
        detections->push_back(std::move(detection));
    }
    return true;
}

float BoxIou(
    const std::array<float, 4>& first,
    const std::array<float, 4>& second) {
    const float intersectionWidth = std::max(
        0.0F, std::min(first[2], second[2]) - std::max(first[0], second[0]));
    const float intersectionHeight = std::max(
        0.0F, std::min(first[3], second[3]) - std::max(first[1], second[1]));
    const float intersection = intersectionWidth * intersectionHeight;
    const float firstArea = std::max(0.0F, first[2] - first[0]) *
                            std::max(0.0F, first[3] - first[1]);
    const float secondArea = std::max(0.0F, second[2] - second[0]) *
                             std::max(0.0F, second[3] - second[1]);
    const float unionArea = firstArea + secondArea - intersection;
    return unionArea > 0.0F ? intersection / unionArea : 0.0F;
}

ComparisonResult CompareDetections(
    const std::vector<Detection>& expected,
    const std::vector<Detection>& actual,
    const ModelContract& contract) {
    if (expected.size() != actual.size()) {
        return {
            false,
            "Detection count differs: expected " +
                std::to_string(expected.size()) + ", actual " +
                std::to_string(actual.size()),
        };
    }
    std::vector<size_t> expectedOrder(expected.size());
    for (size_t index = 0; index < expectedOrder.size(); ++index) {
        expectedOrder[index] = index;
    }
    std::sort(
        expectedOrder.begin(),
        expectedOrder.end(),
        [&](size_t first, size_t second) {
            return expected[first].confidence > expected[second].confidence;
        });
    std::vector<bool> used(actual.size(), false);
    for (size_t expectedIndex : expectedOrder) {
        size_t bestIndex = actual.size();
        float bestIou = -1.0F;
        for (size_t actualIndex = 0; actualIndex < actual.size(); ++actualIndex) {
            if (used[actualIndex] ||
                actual[actualIndex].classId != expected[expectedIndex].classId ||
                actual[actualIndex].className !=
                    expected[expectedIndex].className) {
                continue;
            }
            const float iou = BoxIou(
                expected[expectedIndex].boxXyxy,
                actual[actualIndex].boxXyxy);
            if (iou > bestIou) {
                bestIou = iou;
                bestIndex = actualIndex;
            }
        }
        if (bestIndex == actual.size()) {
            return {
                false,
                "No exact class ID and label match for expected class ID " +
                    std::to_string(expected[expectedIndex].classId) +
                    " (" + expected[expectedIndex].className + ")",
            };
        }
        const float confidenceDelta = std::fabs(
            expected[expectedIndex].confidence - actual[bestIndex].confidence);
        if (confidenceDelta > contract.maximumAbsoluteConfidenceDelta) {
            std::ostringstream message;
            message << "Confidence delta " << confidenceDelta
                    << " exceeds "
                    << contract.maximumAbsoluteConfidenceDelta;
            return {false, message.str()};
        }
        if (bestIou < contract.minimumBoxIou) {
            std::ostringstream message;
            message << "Box IoU " << bestIou << " is below "
                    << contract.minimumBoxIou;
            return {false, message.str()};
        }
        used[bestIndex] = true;
    }
    return {true, "Detections match"};
}

}  // namespace questlab::rfdetr
