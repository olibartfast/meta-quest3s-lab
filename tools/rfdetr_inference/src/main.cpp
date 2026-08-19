#include "camera_source/fixture_manifest.h"
#include "rfdetr_inference/detection_io.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/onnx_session.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"
#include "rfdetr_inference/timing.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path manifestPath;
    std::filesystem::path fixturePath;
    std::filesystem::path modelPath;
    std::filesystem::path outputPrefix = "rfdetr-output";
    std::filesystem::path dumpRgbaPath;
    std::filesystem::path expectedPath;
    int iterations = 10;
    bool validateOnly = false;
    bool validateModelOnly = false;
};

void PrintUsage() {
    std::cerr
        << "Usage: rfdetr_inference --manifest <model-manifest.json> "
           "--fixture <manifest.qcam> [options]\n"
        << "Options:\n"
        << "  --model <rfdetr-nano.onnx>     Run pinned ONNX Runtime inference\n"
        << "  --output-prefix <path>          JSON/PPM output prefix\n"
        << "  --iterations <count>            Measured runs after one warm-up\n"
        << "  --expected <detections.json>    Enforce manifest tolerances\n"
        << "  --dump-rgba <frame.rgba>        Write canonical converted pixels\n"
        << "  --validate-only                 Validate and preprocess without model\n"
        << "  --validate-model-only           Validate model hash and ORT metadata\n";
}

bool ParsePositiveInt(const std::string& value, int* parsed) {
    char* end = nullptr;
    const long result = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || result <= 0 ||
        result > std::numeric_limits<int>::max()) {
        return false;
    }
    *parsed = static_cast<int>(result);
    return true;
}

bool ParseOptions(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto readPath = [&](std::filesystem::path* path) {
            if (index + 1 >= argc) {
                return false;
            }
            *path = argv[++index];
            return true;
        };
        if (argument == "--manifest") {
            if (!readPath(&options->manifestPath)) return false;
        } else if (argument == "--fixture") {
            if (!readPath(&options->fixturePath)) return false;
        } else if (argument == "--model") {
            if (!readPath(&options->modelPath)) return false;
        } else if (argument == "--output-prefix") {
            if (!readPath(&options->outputPrefix)) return false;
        } else if (argument == "--dump-rgba") {
            if (!readPath(&options->dumpRgbaPath)) return false;
        } else if (argument == "--expected") {
            if (!readPath(&options->expectedPath)) return false;
        } else if (argument == "--iterations") {
            if (index + 1 >= argc ||
                !ParsePositiveInt(argv[++index], &options->iterations)) {
                return false;
            }
        } else if (argument == "--validate-only") {
            options->validateOnly = true;
        } else if (argument == "--validate-model-only") {
            options->validateModelOnly = true;
        } else {
            return false;
        }
    }
    if (options->manifestPath.empty()) {
        return false;
    }
    if (options->validateModelOnly) {
        return !options->modelPath.empty() && options->fixturePath.empty() &&
               !options->validateOnly;
    }
    return !options->fixturePath.empty() &&
           (options->validateOnly || !options->modelPath.empty());
}

template <typename Function>
double MeasureMilliseconds(Function&& function) {
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

std::filesystem::path WithSuffix(
    const std::filesystem::path& prefix,
    const std::string& suffix) {
    return std::filesystem::path(prefix.string() + suffix);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        PrintUsage();
        return 2;
    }

    std::string error;
    questlab::rfdetr::ModelContract contract;
    if (!questlab::rfdetr::LoadModelContract(
            options.manifestPath, &contract, &error)) {
        std::cerr << "Model manifest rejected: " << error << '\n';
        return 1;
    }
    if (options.validateModelOnly) {
        questlab::rfdetr::OnnxSession session;
        if (!session.Initialize(options.modelPath, contract, &error)) {
            std::cerr << "ONNX Runtime initialization failed: " << error
                      << '\n';
            return 1;
        }
        size_t inputElementCount = 1;
        for (const int64_t dimension : contract.input.shape) {
            inputElementCount *= static_cast<size_t>(dimension);
        }
        const std::vector<float> inputTensor(inputElementCount, 0.0F);
        std::vector<float> boxes;
        std::vector<float> logits;
        if (!session.Run(inputTensor, &boxes, &logits, &error)) {
            std::cerr << "ONNX Runtime smoke inference failed: " << error
                      << '\n';
            return 1;
        }
        std::cout << "Model hash, tensor metadata, ONNX Runtime contract, and "
                     "smoke inference validated; boxes="
                  << boxes.size() << ", logits=" << logits.size() << '\n';
        return 0;
    }
    questlab::camera::RgbCapture capture;
    if (!questlab::camera::LoadQuestCameraFixture(
            options.fixturePath, &capture, &error)) {
        std::cerr << "Quest fixture rejected: " << error << '\n';
        return 1;
    }
    const std::string pixelSha256 =
        questlab::camera::ComputeQuestCameraPixelSha256(capture);

    std::vector<uint8_t> rgba;
    std::vector<float> inputTensor;
    if (!questlab::rfdetr::PreprocessCapture(
            capture, contract, &rgba, &inputTensor, &error)) {
        std::cerr << "Preprocessing failed: " << error << '\n';
        return 1;
    }
    if (!options.dumpRgbaPath.empty() &&
        !questlab::rfdetr::WriteRawRgba(
            options.dumpRgbaPath, rgba, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (options.validateOnly) {
        std::cout << "Fixture and model contract validated; pixel_sha256="
                  << pixelSha256 << ", tensor_values=" << inputTensor.size()
                  << '\n';
        return 0;
    }

    questlab::rfdetr::OnnxSession session;
    if (!session.Initialize(options.modelPath, contract, &error)) {
        std::cerr << "ONNX Runtime initialization failed: " << error << '\n';
        return 1;
    }
    std::vector<float> boxes;
    std::vector<float> logits;
    if (!session.Run(inputTensor, &boxes, &logits, &error)) {
        std::cerr << "ONNX Runtime warm-up failed: " << error << '\n';
        return 1;
    }

    std::vector<double> preprocessingTimes;
    std::vector<double> inferenceTimes;
    std::vector<double> postprocessingTimes;
    std::vector<questlab::rfdetr::Detection> detections;
    preprocessingTimes.reserve(static_cast<size_t>(options.iterations));
    inferenceTimes.reserve(static_cast<size_t>(options.iterations));
    postprocessingTimes.reserve(static_cast<size_t>(options.iterations));
    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        bool preprocessSucceeded = false;
        preprocessingTimes.push_back(MeasureMilliseconds([&] {
            preprocessSucceeded = questlab::rfdetr::PreprocessCapture(
                capture, contract, &rgba, &inputTensor, &error);
        }));
        if (!preprocessSucceeded) {
            std::cerr << "Measured preprocessing failed: " << error << '\n';
            return 1;
        }

        bool inferenceSucceeded = false;
        inferenceTimes.push_back(MeasureMilliseconds([&] {
            inferenceSucceeded =
                session.Run(inputTensor, &boxes, &logits, &error);
        }));
        if (!inferenceSucceeded) {
            std::cerr << "Measured inference failed: " << error << '\n';
            return 1;
        }

        bool postprocessingSucceeded = false;
        postprocessingTimes.push_back(MeasureMilliseconds([&] {
            postprocessingSucceeded = questlab::rfdetr::PostprocessDetections(
                boxes,
                logits,
                capture.width,
                capture.height,
                contract,
                &detections,
                &error);
        }));
        if (!postprocessingSucceeded) {
            std::cerr << "Postprocessing failed: " << error << '\n';
            return 1;
        }
    }

    const questlab::rfdetr::TimingSummary preprocessing =
        questlab::rfdetr::SummarizeTimings(preprocessingTimes);
    const questlab::rfdetr::TimingSummary inference =
        questlab::rfdetr::SummarizeTimings(inferenceTimes);
    const questlab::rfdetr::TimingSummary postprocessing =
        questlab::rfdetr::SummarizeTimings(postprocessingTimes);

    const std::filesystem::path outputDirectory =
        options.outputPrefix.parent_path();
    if (!outputDirectory.empty()) {
        std::error_code filesystemError;
        std::filesystem::create_directories(outputDirectory, filesystemError);
        if (filesystemError) {
            std::cerr << "Cannot create output directory: "
                      << filesystemError.message() << '\n';
            return 1;
        }
    }
    const std::filesystem::path jsonPath =
        WithSuffix(options.outputPrefix, ".json");
    const std::filesystem::path previewPath =
        WithSuffix(options.outputPrefix, ".ppm");
    if (!questlab::rfdetr::WriteDetectionJson(
            jsonPath,
            capture.width,
            capture.height,
            pixelSha256,
            detections,
            preprocessing,
            inference,
            postprocessing,
            contract,
            &error) ||
        !questlab::rfdetr::WriteAnnotatedPpm(
            previewPath,
            capture.width,
            capture.height,
            rgba,
            detections,
            &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    if (!options.expectedPath.empty()) {
        std::vector<questlab::rfdetr::Detection> expected;
        if (!questlab::rfdetr::LoadDetectionJson(
                options.expectedPath, &expected, &error)) {
            std::cerr << "Expected detections rejected: " << error << '\n';
            return 1;
        }
        const questlab::rfdetr::ComparisonResult comparison =
            questlab::rfdetr::CompareDetections(expected, detections, contract);
        if (!comparison.matches) {
            std::cerr << "Reference comparison failed: "
                      << comparison.message << '\n';
            return 1;
        }
    }

    std::cout << "Wrote " << detections.size() << " detections to "
              << jsonPath << " and " << previewPath << '\n';
    return 0;
}
