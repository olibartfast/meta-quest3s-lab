#include "rfdetr_inference/model_contract.h"

#include "artifact_integrity/sha256.h"
#include "json_value.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace questlab::rfdetr {
namespace {

using internal::JsonValue;

int32_t ReadInt32(const JsonValue& value, const std::string& context) {
    const double number = value.AsNumber(context);
    if (std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error("Expected 32-bit integer: " + context);
    }
    return static_cast<int32_t>(number);
}

std::vector<int64_t> ReadShape(
    const JsonValue& value,
    const std::string& context) {
    std::vector<int64_t> result;
    for (const JsonValue& dimension : value.AsArray(context)) {
        const double number = dimension.AsNumber(context + " dimension");
        if (std::floor(number) != number || number <= 0.0 ||
            number > static_cast<double>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error(
                "Tensor shape dimensions must be positive integers: " + context);
        }
        result.push_back(static_cast<int64_t>(number));
    }
    return result;
}

std::array<float, 3> ReadFloat3(
    const JsonValue& value,
    const std::string& context) {
    const std::vector<JsonValue>& array = value.AsArray(context);
    if (array.size() != 3U) {
        throw std::runtime_error("Expected three values: " + context);
    }
    std::array<float, 3> result{};
    for (size_t index = 0; index < result.size(); ++index) {
        const double number = array[index].AsNumber(context);
        if (!std::isfinite(number) ||
            number < -static_cast<double>(std::numeric_limits<float>::max()) ||
            number > static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::runtime_error("Invalid float value: " + context);
        }
        result[index] = static_cast<float>(number);
    }
    return result;
}

TensorContract ReadTensor(
    const JsonValue& value,
    const std::string& context) {
    TensorContract tensor;
    tensor.name = value.At("name").AsString(context + ".name");
    tensor.elementType =
        value.At("element_type").AsString(context + ".element_type");
    tensor.shape = ReadShape(value.At("shape"), context + ".shape");
    const auto meaning = value.object.find("meaning");
    if (meaning != value.object.end()) {
        tensor.meaning = meaning->second.AsString(context + ".meaning");
    }
    return tensor;
}

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("Unsupported RF-DETR contract: " + message);
    }
}

bool NearlyEqual(float first, float second) {
    return std::fabs(first - second) <= 1.0e-7F;
}

void ValidateSupportedContract(const ModelContract& contract) {
    Require(contract.schema == "QUESTLAB_RFDETR_MODEL_V1", "schema");
    Require(contract.family == "RF-DETR", "model family");
    Require(contract.variant == "nano", "model variant");
    Require(contract.onnxOpset == 17, "ONNX opset");
    Require(
        questlab::integrity::IsLowercaseSha256(contract.onnxSha256),
        "ONNX SHA-256");
    Require(contract.input.name == "input", "input name");
    Require(contract.input.elementType == "float32", "input element type");
    Require(
        contract.input.shape == std::vector<int64_t>({1, 3, 384, 384}),
        "input shape");
    Require(contract.layout == "NCHW", "input layout");
    Require(contract.sourceColorOrder == "RGB", "input colour order");
    Require(NearlyEqual(contract.scale, 1.0F / 255.0F), "input scale");
    Require(contract.resizeMode == "squash", "resize mode");
    Require(contract.interpolation == "bilinear", "resize interpolation");
    Require(
        contract.coordinateTransform == "half_pixel",
        "resize coordinate transform");
    Require(!contract.antialias, "resize antialias setting");
    Require(contract.outputs.size() == 2U, "output count");
    Require(contract.outputs[0].name == "dets", "box output name");
    Require(
        contract.outputs[0].elementType == "float32" &&
        contract.outputs[0].shape == std::vector<int64_t>({1, 300, 4}) &&
        contract.outputs[0].meaning == "normalized_cxcywh",
        "box output contract");
    Require(contract.outputs[1].name == "labels", "label output name");
    Require(
        contract.outputs[1].elementType == "float32" &&
        contract.outputs[1].shape == std::vector<int64_t>({1, 300, 91}) &&
        contract.outputs[1].meaning == "per_query_class_logits",
        "label output contract");
    Require(
        contract.classIndexSpace == "coco_sparse_category_id" &&
        contract.classLogitSlots == 91 && contract.classNames.size() == 80U,
        "COCO sparse class space");
    Require(contract.scoreTransform == "sigmoid", "score transform");
    Require(
        contract.selection == "global_top_k_query_class_pairs",
        "detection selection");
    Require(contract.confidenceThreshold >= 0.0F &&
            contract.confidenceThreshold <= 1.0F,
            "confidence threshold");
    Require(contract.maximumDetections > 0 &&
            contract.maximumDetections <= 300,
            "maximum detections");
    Require(!contract.nms, "NMS must remain disabled");
    Require(
        contract.boxMapping ==
            "normalized_cxcywh_direct_to_source_xyxy_and_clip",
        "box mapping");
    Require(contract.runtimeVersion == "1.21.0", "ONNX Runtime version");
    Require(
        contract.executionProvider == "CPUExecutionProvider",
        "execution provider");
    Require(contract.intraOpThreads == 1 && contract.interOpThreads == 1,
            "runtime thread counts");
    Require(contract.executionMode == "sequential", "execution mode");
    Require(contract.graphOptimization == "extended", "graph optimization");
    Require(
        contract.matching ==
            "greedy_descending_confidence_same_class_highest_iou",
        "comparison matching rule");
    Require(contract.maximumAbsoluteConfidenceDelta >= 0.0F,
            "confidence comparison tolerance");
    Require(contract.minimumBoxIou >= 0.0F && contract.minimumBoxIou <= 1.0F,
            "box IoU tolerance");
    Require(contract.detectionCountRule == "exact", "detection count rule");
}

}  // namespace

bool LoadModelContract(
    const std::filesystem::path& path,
    ModelContract* contract,
    std::string* error) {
    if (contract == nullptr) {
        if (error != nullptr) {
            *error = "Model contract output pointer is null";
        }
        return false;
    }
    std::ifstream stream(path);
    if (!stream) {
        if (error != nullptr) {
            *error = "Cannot open model manifest: " + path.string();
        }
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    JsonValue root;
    if (!internal::ParseJson(contents.str(), &root, error)) {
        return false;
    }

    try {
        ModelContract loaded;
        loaded.schema = root.At("schema").AsString("schema");
        Require(ReadInt32(root.At("manifest_version"), "manifest_version") == 1,
                "manifest version");

        const JsonValue& model = root.At("model");
        loaded.family = model.At("family").AsString("model.family");
        loaded.variant = model.At("variant").AsString("model.variant");
        const JsonValue& onnx = model.At("onnx");
        loaded.onnxFilename =
            onnx.At("filename").AsString("model.onnx.filename");
        loaded.onnxOpset = ReadInt32(onnx.At("opset"), "model.onnx.opset");
        loaded.onnxSha256 =
            onnx.At("sha256").AsString("model.onnx.sha256");

        const JsonValue& input = root.At("input");
        loaded.input = ReadTensor(input, "input");
        loaded.layout = input.At("layout").AsString("input.layout");
        loaded.sourceColorOrder =
            input.At("source_color_order").AsString("input.source_color_order");
        loaded.scale = static_cast<float>(input.At("scale").AsNumber("input.scale"));
        const JsonValue& normalization = input.At("normalization");
        loaded.mean = ReadFloat3(normalization.At("mean"), "input.mean");
        loaded.standardDeviation =
            ReadFloat3(normalization.At("std"), "input.std");
        const JsonValue& resize = input.At("resize");
        loaded.resizeMode = resize.At("mode").AsString("input.resize.mode");
        Require(
            !resize.At("preserve_aspect_ratio").AsBoolean(
                "input.resize.preserve_aspect_ratio"),
            "aspect ratio preservation");
        Require(
            resize.At("padding").AsString("input.resize.padding") == "none",
            "resize padding");
        loaded.interpolation =
            resize.At("interpolation").AsString("input.resize.interpolation");
        loaded.coordinateTransform = resize.At("coordinate_transform").AsString(
            "input.resize.coordinate_transform");
        loaded.antialias =
            resize.At("antialias").AsBoolean("input.resize.antialias");

        for (const JsonValue& output :
             root.At("outputs").AsArray("outputs")) {
            loaded.outputs.push_back(ReadTensor(output, "output"));
        }

        const JsonValue& classes = root.At("classes");
        loaded.classIndexSpace =
            classes.At("index_space").AsString("classes.index_space");
        loaded.classLogitSlots =
            ReadInt32(classes.At("logit_slots"), "classes.logit_slots");
        for (const JsonValue& entry :
             classes.At("entries").AsArray("classes.entries")) {
            const int32_t classId =
                ReadInt32(entry.At("id"), "classes.entries.id");
            const std::string className =
                entry.At("name").AsString("classes.entries.name");
            Require(classId >= 0 && classId < loaded.classLogitSlots,
                    "class ID range");
            Require(!className.empty(), "empty class name");
            Require(loaded.classNames.emplace(classId, className).second,
                    "duplicate class ID");
        }

        const JsonValue& postprocessing = root.At("postprocessing");
        loaded.scoreTransform = postprocessing.At("score_transform").AsString(
            "postprocessing.score_transform");
        loaded.selection = postprocessing.At("selection").AsString(
            "postprocessing.selection");
        loaded.confidenceThreshold = static_cast<float>(
            postprocessing.At("confidence_threshold").AsNumber(
                "postprocessing.confidence_threshold"));
        loaded.maximumDetections = ReadInt32(
            postprocessing.At("maximum_detections"),
            "postprocessing.maximum_detections");
        loaded.nms =
            postprocessing.At("nms").AsBoolean("postprocessing.nms");
        loaded.boxMapping = postprocessing.At("box_mapping").AsString(
            "postprocessing.box_mapping");

        const JsonValue& runtime = root.At("runtime");
        loaded.runtimeVersion =
            runtime.At("version").AsString("runtime.version");
        loaded.executionProvider = runtime.At("execution_provider").AsString(
            "runtime.execution_provider");
        loaded.intraOpThreads = ReadInt32(
            runtime.At("intra_op_threads"), "runtime.intra_op_threads");
        loaded.interOpThreads = ReadInt32(
            runtime.At("inter_op_threads"), "runtime.inter_op_threads");
        loaded.executionMode =
            runtime.At("execution_mode").AsString("runtime.execution_mode");
        loaded.graphOptimization = runtime.At("graph_optimization").AsString(
            "runtime.graph_optimization");

        const JsonValue& comparison = root.At("comparison");
        loaded.matching =
            comparison.At("matching").AsString("comparison.matching");
        Require(
            comparison.At("class_id_agreement").AsString(
                "comparison.class_id_agreement") == "exact",
            "class ID comparison rule");
        loaded.maximumAbsoluteConfidenceDelta = static_cast<float>(
            comparison.At("maximum_absolute_confidence_delta").AsNumber(
                "comparison.maximum_absolute_confidence_delta"));
        loaded.minimumBoxIou = static_cast<float>(
            comparison.At("minimum_box_iou").AsNumber(
                "comparison.minimum_box_iou"));
        loaded.detectionCountRule = comparison.At("detection_count").AsString(
            "comparison.detection_count");

        ValidateSupportedContract(loaded);
        *contract = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool ValidateModelArtifact(
    const std::filesystem::path& modelPath,
    const ModelContract& contract,
    std::string* error) {
    std::string actualSha256;
    if (!questlab::integrity::Sha256File(modelPath, &actualSha256, error)) {
        return false;
    }
    if (actualSha256 != contract.onnxSha256) {
        if (error != nullptr) {
            *error = "ONNX SHA-256 mismatch: expected " + contract.onnxSha256 +
                     ", actual " + actualSha256;
        }
        return false;
    }
    return true;
}

}  // namespace questlab::rfdetr
