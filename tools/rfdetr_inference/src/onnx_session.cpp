#include "rfdetr_inference/onnx_session.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#if defined(QUESTLAB_HAS_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_session_options_config_keys.h>
#endif

namespace questlab::rfdetr {

class OnnxSession::Impl {
public:
#if defined(QUESTLAB_HAS_ONNXRUNTIME)
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "QuestLabRFDETR"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName;
    std::vector<std::string> outputNames;
    std::vector<const char*> outputNamePointers;
    std::vector<int64_t> inputShape;
    std::mutex runOptionsMutex;
    Ort::RunOptions* activeRunOptions = nullptr;
    std::atomic<bool> cancellationRequested{false};
#endif
    std::string runtimeDescription;
    bool initialized = false;
};

OnnxSession::OnnxSession() : impl_(std::make_unique<Impl>()) {}
OnnxSession::~OnnxSession() = default;
OnnxSession::OnnxSession(OnnxSession&&) noexcept = default;
OnnxSession& OnnxSession::operator=(OnnxSession&&) noexcept = default;

bool OnnxSession::Initialize(
    const std::filesystem::path& modelPath,
    const ModelContract& contract,
    std::string* error) {
    return Initialize(modelPath, contract, RuntimeOptions{}, error);
}

bool OnnxSession::Initialize(
    const std::filesystem::path& modelPath,
    const ModelContract& contract,
    const RuntimeOptions& runtimeOptions,
    std::string* error) {
#if !defined(QUESTLAB_HAS_ONNXRUNTIME)
    (void)modelPath;
    (void)contract;
    (void)runtimeOptions;
    if (error != nullptr) {
        *error = "This build has no ONNX Runtime backend; configure with "
                 "-DQUESTLAB_ENABLE_ONNXRUNTIME=ON and the pinned "
                 "ONNXRUNTIME_ROOT";
    }
    return false;
#else
    try {
        if (!ValidateModelArtifact(modelPath, contract, error)) {
            return false;
        }
        const std::string runtimeVersion = OrtGetApiBase()->GetVersionString();
        if (runtimeVersion != contract.runtimeVersion) {
            if (error != nullptr) {
                *error = "ONNX Runtime version mismatch: expected " +
                         contract.runtimeVersion + ", actual " + runtimeVersion;
            }
            return false;
        }

        const std::string executionProvider =
            runtimeOptions.executionProvider.empty()
                ? contract.executionProvider
                : runtimeOptions.executionProvider;
        impl_->runtimeDescription =
            "ONNX Runtime " + runtimeVersion + " selected=" +
            executionProvider + " available=";
        const std::vector<std::string> availableProviders =
            Ort::GetAvailableProviders();
        for (size_t index = 0; index < availableProviders.size(); ++index) {
            if (index != 0U) {
                impl_->runtimeDescription += ',';
            }
            impl_->runtimeDescription += availableProviders[index];
        }
        if (executionProvider == "XNNPACK" ||
            executionProvider == "XnnpackExecutionProvider") {
            const int32_t xnnpackThreads =
                std::max(runtimeOptions.xnnpackThreads, 1);
            impl_->options.AddConfigEntry(
                kOrtSessionOptionsConfigAllowIntraOpSpinning, "0");
            impl_->options.SetIntraOpNumThreads(1);
            impl_->options.AppendExecutionProvider(
                "XNNPACK",
                {{"intra_op_num_threads", std::to_string(xnnpackThreads)}});
        } else if (executionProvider == "CPU" ||
                   executionProvider == "CPUExecutionProvider") {
            impl_->options.SetIntraOpNumThreads(contract.intraOpThreads);
        } else {
            throw std::runtime_error(
                "Unsupported ONNX Runtime execution provider: " +
                executionProvider);
        }
        impl_->options.SetInterOpNumThreads(contract.interOpThreads);
        impl_->options.SetExecutionMode(ORT_SEQUENTIAL);
        impl_->options.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
        impl_->session = std::make_unique<Ort::Session>(
            impl_->environment, modelPath.c_str(), impl_->options);

        Ort::AllocatorWithDefaultOptions allocator;
        if (impl_->session->GetInputCount() != 1U ||
            impl_->session->GetOutputCount() != contract.outputs.size()) {
            throw std::runtime_error("ONNX model input/output count mismatch");
        }
        Ort::AllocatedStringPtr inputName =
            impl_->session->GetInputNameAllocated(0, allocator);
        impl_->inputName = inputName.get();
        if (impl_->inputName != contract.input.name) {
            throw std::runtime_error(
                "ONNX input name mismatch: " + impl_->inputName);
        }
        const Ort::TypeInfo inputType = impl_->session->GetInputTypeInfo(0);
        const auto inputInfo = inputType.GetTensorTypeAndShapeInfo();
        if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            inputInfo.GetShape() != contract.input.shape) {
            throw std::runtime_error("ONNX input type or shape mismatch");
        }
        impl_->inputShape = contract.input.shape;

        impl_->outputNames.clear();
        impl_->outputNamePointers.clear();
        for (size_t index = 0; index < contract.outputs.size(); ++index) {
            Ort::AllocatedStringPtr outputName =
                impl_->session->GetOutputNameAllocated(index, allocator);
            impl_->outputNames.emplace_back(outputName.get());
            if (impl_->outputNames.back() != contract.outputs[index].name) {
                throw std::runtime_error(
                    "ONNX output name mismatch at index " +
                    std::to_string(index));
            }
            const Ort::TypeInfo outputType =
                impl_->session->GetOutputTypeInfo(index);
            const auto outputInfo = outputType.GetTensorTypeAndShapeInfo();
            if (outputInfo.GetElementType() !=
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                outputInfo.GetShape() != contract.outputs[index].shape) {
                throw std::runtime_error(
                    "ONNX output type or shape mismatch at index " +
                    std::to_string(index));
            }
        }
        for (const std::string& outputName : impl_->outputNames) {
            impl_->outputNamePointers.push_back(outputName.c_str());
        }
        impl_->initialized = true;
        impl_->cancellationRequested.store(false);
        return true;
    } catch (const std::exception& exception) {
        impl_->session.reset();
        impl_->initialized = false;
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
#endif
}

bool OnnxSession::Run(
    const std::vector<float>& inputTensor,
    std::vector<float>* boxes,
    std::vector<float>* logits,
    std::string* error) {
#if !defined(QUESTLAB_HAS_ONNXRUNTIME)
    (void)inputTensor;
    (void)boxes;
    (void)logits;
    if (error != nullptr) {
        *error = "ONNX Runtime backend is unavailable";
    }
    return false;
#else
    if (!impl_->initialized || impl_->session == nullptr ||
        boxes == nullptr || logits == nullptr) {
        if (error != nullptr) {
            *error = "ONNX Runtime session or output pointer is invalid";
        }
        return false;
    }
    const size_t expectedInputSize = std::accumulate(
        impl_->inputShape.begin(),
        impl_->inputShape.end(),
        size_t{1},
        [](size_t product, int64_t dimension) {
            return product * static_cast<size_t>(dimension);
        });
    if (inputTensor.size() != expectedInputSize) {
        if (error != nullptr) {
            *error = "Input tensor size does not match ONNX metadata";
        }
        return false;
    }
    try {
        if (impl_->cancellationRequested.load()) {
            if (error != nullptr) {
                *error = "ONNX Runtime inference was cancelled";
            }
            return false;
        }
        Ort::Value input = Ort::Value::CreateTensor<float>(
            impl_->memoryInfo,
            const_cast<float*>(inputTensor.data()),
            inputTensor.size(),
            impl_->inputShape.data(),
            impl_->inputShape.size());
        const char* inputName = impl_->inputName.c_str();
        Ort::RunOptions runOptions;
        {
            std::lock_guard<std::mutex> lock(impl_->runOptionsMutex);
            impl_->activeRunOptions = &runOptions;
            if (impl_->cancellationRequested.load()) {
                runOptions.SetTerminate();
            }
        }
        std::vector<Ort::Value> outputs = impl_->session->Run(
            runOptions,
            &inputName,
            &input,
            1,
            impl_->outputNamePointers.data(),
            impl_->outputNamePointers.size());
        {
            std::lock_guard<std::mutex> lock(impl_->runOptionsMutex);
            impl_->activeRunOptions = nullptr;
        }
        if (outputs.size() != 2U) {
            throw std::runtime_error("ONNX Runtime returned an output count mismatch");
        }
        const size_t boxCount = outputs[0].GetTensorTypeAndShapeInfo()
                                    .GetElementCount();
        const size_t logitCount = outputs[1].GetTensorTypeAndShapeInfo()
                                      .GetElementCount();
        const float* boxData = outputs[0].GetTensorData<float>();
        const float* logitData = outputs[1].GetTensorData<float>();
        boxes->assign(boxData, boxData + boxCount);
        logits->assign(logitData, logitData + logitCount);
        return true;
    } catch (const std::exception& exception) {
        {
            std::lock_guard<std::mutex> lock(impl_->runOptionsMutex);
            impl_->activeRunOptions = nullptr;
        }
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
#endif
}

void OnnxSession::RequestCancellation() {
#if defined(QUESTLAB_HAS_ONNXRUNTIME)
    impl_->cancellationRequested.store(true);
    std::lock_guard<std::mutex> lock(impl_->runOptionsMutex);
    if (impl_->activeRunOptions != nullptr) {
        impl_->activeRunOptions->SetTerminate();
    }
#endif
}

bool OnnxSession::IsAvailable() const {
#if defined(QUESTLAB_HAS_ONNXRUNTIME)
    return true;
#else
    return false;
#endif
}

std::string OnnxSession::RuntimeDescription() const {
    return impl_->runtimeDescription;
}

}  // namespace questlab::rfdetr
