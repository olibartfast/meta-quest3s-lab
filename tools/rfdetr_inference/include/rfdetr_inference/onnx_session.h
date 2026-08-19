#pragma once

#include "rfdetr_inference/model_contract.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace questlab::rfdetr {

struct RuntimeOptions {
    std::string executionProvider;
    int32_t xnnpackThreads = 0;
};

class OnnxSession {
public:
    OnnxSession();
    ~OnnxSession();
    OnnxSession(OnnxSession&&) noexcept;
    OnnxSession& operator=(OnnxSession&&) noexcept;

    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    bool Initialize(
        const std::filesystem::path& modelPath,
        const ModelContract& contract,
        std::string* error);

    bool Initialize(
        const std::filesystem::path& modelPath,
        const ModelContract& contract,
        const RuntimeOptions& runtimeOptions,
        std::string* error);

    bool Run(
        const std::vector<float>& inputTensor,
        std::vector<float>* boxes,
        std::vector<float>* logits,
        std::string* error);

    bool IsAvailable() const;
    void RequestCancellation();
    std::string RuntimeDescription() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab::rfdetr
