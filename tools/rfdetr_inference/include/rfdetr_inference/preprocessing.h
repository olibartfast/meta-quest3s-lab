#pragma once

#include "camera_source/camera_source.h"
#include "rfdetr_inference/model_contract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace questlab::rfdetr {

bool PreprocessCapture(
    const questlab::camera::RgbCapture& capture,
    const ModelContract& contract,
    std::vector<uint8_t>* rgba,
    std::vector<float>* inputTensor,
    std::string* error);

}  // namespace questlab::rfdetr
