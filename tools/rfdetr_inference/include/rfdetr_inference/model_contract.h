#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace questlab::rfdetr {

struct TensorContract {
    std::string name;
    std::string elementType;
    std::vector<int64_t> shape;
    std::string meaning;
};

struct ModelContract {
    std::string schema;
    std::string family;
    std::string variant;
    std::string onnxFilename;
    std::string onnxSha256;
    int32_t onnxOpset = 0;

    TensorContract input;
    std::string layout;
    std::string sourceColorOrder;
    float scale = 0.0F;
    std::array<float, 3> mean{};
    std::array<float, 3> standardDeviation{};
    std::string resizeMode;
    std::string interpolation;
    std::string coordinateTransform;
    bool antialias = true;

    std::vector<TensorContract> outputs;
    std::string classIndexSpace;
    int32_t classLogitSlots = 0;
    std::unordered_map<int32_t, std::string> classNames;

    std::string scoreTransform;
    std::string selection;
    float confidenceThreshold = 0.0F;
    int32_t maximumDetections = 0;
    bool nms = true;
    std::string boxMapping;

    std::string runtimeVersion;
    std::string executionProvider;
    int32_t intraOpThreads = 0;
    int32_t interOpThreads = 0;
    std::string executionMode;
    std::string graphOptimization;

    std::string matching;
    float maximumAbsoluteConfidenceDelta = 0.0F;
    float minimumBoxIou = 0.0F;
    std::string detectionCountRule;
};

bool LoadModelContract(
    const std::filesystem::path& path,
    ModelContract* contract,
    std::string* error);

bool ValidateModelArtifact(
    const std::filesystem::path& modelPath,
    const ModelContract& contract,
    std::string* error);

}  // namespace questlab::rfdetr
