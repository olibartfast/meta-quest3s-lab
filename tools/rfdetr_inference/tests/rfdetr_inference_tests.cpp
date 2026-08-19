#include "camera_source/camera_source.h"
#include "camera_source/yuv_converter.h"
#include "artifact_integrity/sha256.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"
#include "rfdetr_inference/timing.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

void TestSha256KnownVector() {
    constexpr char input[] = "abc";
    assert(questlab::integrity::Sha256Hex(input, 3) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

questlab::rfdetr::ModelContract LoadContract() {
    questlab::rfdetr::ModelContract contract;
    std::string error;
    assert(questlab::rfdetr::LoadModelContract(
        QUESTLAB_RFDETR_MANIFEST_PATH, &contract, &error));
    return contract;
}

questlab::camera::RgbCapture MakeGrayCapture() {
    questlab::camera::RgbCapture capture;
    capture.width = 2;
    capture.height = 2;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0].bytes = {16, 235, 81, 145};
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

void TestPinnedManifestLoads() {
    const questlab::rfdetr::ModelContract contract = LoadContract();
    assert(contract.input.shape == std::vector<int64_t>({1, 3, 384, 384}));
    assert(contract.outputs[0].name == "dets");
    assert(contract.outputs[1].name == "labels");
    assert(contract.classNames.at(1) == "person");
    assert(contract.classNames.at(90) == "toothbrush");
    assert(contract.classNames.find(12) == contract.classNames.end());
    assert(!contract.nms);
}

void TestPreprocessingMatchesSavedReferenceValues() {
    questlab::rfdetr::ModelContract contract = LoadContract();
    contract.input.shape = {1, 3, 3, 3};
    const questlab::camera::RgbCapture capture = MakeGrayCapture();
    std::vector<uint8_t> rgba;
    std::vector<float> tensor;
    std::string error;
    assert(questlab::rfdetr::PreprocessCapture(
        capture, contract, &rgba, &tensor, &error));
    assert(rgba == std::vector<uint8_t>({
        0, 0, 0, 255,
        255, 255, 255, 255,
        76, 76, 76, 255,
        150, 150, 150, 255,
    }));
    assert(tensor.size() == 27U);
    const float expectedTopLeft =
        (0.0F / 255.0F - 0.485F) / 0.229F;
    const float expectedCenter =
        (120.25F / 255.0F - 0.485F) / 0.229F;
    assert(std::fabs(tensor[0] - expectedTopLeft) < 1.0e-6F);
    assert(std::fabs(tensor[4] - expectedCenter) < 1.0e-5F);
}

void TestCoordinateMappingAndSparseClassLabel() {
    const questlab::rfdetr::ModelContract contract = LoadContract();
    std::vector<float> boxes(300U * 4U, 0.0F);
    std::vector<float> logits(300U * 91U, -100.0F);
    boxes[0] = 0.5F;
    boxes[1] = 0.5F;
    boxes[2] = 0.25F;
    boxes[3] = 0.5F;
    logits[90] = 10.0F;
    std::vector<questlab::rfdetr::Detection> detections;
    std::string error;
    assert(questlab::rfdetr::PostprocessDetections(
        boxes, logits, 640, 480, contract, &detections, &error));
    assert(detections.size() == 1U);
    assert(detections[0].classId == 90);
    assert(detections[0].className == "toothbrush");
    assert(std::fabs(detections[0].boxXyxy[0] - 240.0F) < 1.0e-5F);
    assert(std::fabs(detections[0].boxXyxy[1] - 120.0F) < 1.0e-5F);
    assert(std::fabs(detections[0].boxXyxy[2] - 400.0F) < 1.0e-5F);
    assert(std::fabs(detections[0].boxXyxy[3] - 360.0F) < 1.0e-5F);
}

void TestEmptyAndInvalidOutputs() {
    const questlab::rfdetr::ModelContract contract = LoadContract();
    std::vector<float> boxes(300U * 4U, 0.0F);
    std::vector<float> logits(300U * 91U, -100.0F);
    std::vector<questlab::rfdetr::Detection> detections;
    std::string error;
    assert(questlab::rfdetr::PostprocessDetections(
        boxes, logits, 100, 100, contract, &detections, &error));
    assert(detections.empty());
    boxes[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!questlab::rfdetr::PostprocessDetections(
        boxes, logits, 100, 100, contract, &detections, &error));
    assert(error.find("NaN") != std::string::npos);
}

void TestComparisonRules() {
    const questlab::rfdetr::ModelContract contract = LoadContract();
    const questlab::rfdetr::Detection expected{
        1, "person", 0.9F, {10.0F, 20.0F, 50.0F, 80.0F}};
    questlab::rfdetr::Detection actual = expected;
    actual.confidence += contract.maximumAbsoluteConfidenceDelta * 0.5F;
    actual.boxXyxy[0] += 0.001F;
    assert(questlab::rfdetr::CompareDetections(
        {expected}, {actual}, contract).matches);
    actual.classId = 2;
    assert(!questlab::rfdetr::CompareDetections(
        {expected}, {actual}, contract).matches);
    actual = expected;
    actual.className = "bicycle";
    assert(!questlab::rfdetr::CompareDetections(
        {expected}, {actual}, contract).matches);
    assert(!questlab::rfdetr::CompareDetections(
        {expected}, {}, contract).matches);
}

void TestCorruptModelAndManifestMismatch() {
    const questlab::rfdetr::ModelContract contract = LoadContract();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "questlab-rfdetr-contract-tests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path corruptModel = directory / "corrupt.onnx";
    std::ofstream(corruptModel, std::ios::binary).write("not an ONNX model", 17);
    std::string error;
    assert(!questlab::rfdetr::ValidateModelArtifact(
        corruptModel, contract, &error));
    assert(error.find("SHA-256 mismatch") != std::string::npos);

    std::ifstream manifestInput(QUESTLAB_RFDETR_MANIFEST_PATH);
    std::ostringstream contents;
    contents << manifestInput.rdbuf();
    std::string badManifest = contents.str();
    const size_t layout = badManifest.find("\"NCHW\"");
    assert(layout != std::string::npos);
    badManifest.replace(layout, 6, "\"NHWC\"");
    const std::filesystem::path badManifestPath = directory / "bad.json";
    std::ofstream(badManifestPath) << badManifest;
    questlab::rfdetr::ModelContract rejected;
    error.clear();
    assert(!questlab::rfdetr::LoadModelContract(
        badManifestPath, &rejected, &error));
    assert(error.find("input layout") != std::string::npos);
    std::filesystem::remove_all(directory);
}

void TestTimingDistribution() {
    const questlab::rfdetr::TimingSummary summary =
        questlab::rfdetr::SummarizeTimings({1.0, 2.0, 3.0, 4.0, 5.0});
    assert(summary.count == 5U);
    assert(std::fabs(summary.meanMilliseconds - 3.0) < 1.0e-9);
    assert(std::fabs(summary.p50Milliseconds - 3.0) < 1.0e-9);
    assert(std::fabs(summary.maximumMilliseconds - 5.0) < 1.0e-9);
}

}  // namespace

int main() {
    TestSha256KnownVector();
    TestPinnedManifestLoads();
    TestPreprocessingMatchesSavedReferenceValues();
    TestCoordinateMappingAndSparseClassLabel();
    TestEmptyAndInvalidOutputs();
    TestComparisonRules();
    TestCorruptModelAndManifestMismatch();
    TestTimingDistribution();
    return 0;
}
