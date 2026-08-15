#include "rfdetr_inference/preprocessing.h"

#include "camera_source/yuv_converter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace questlab::rfdetr {

bool PreprocessCapture(
    const questlab::camera::RgbCapture& capture,
    const ModelContract& contract,
    std::vector<uint8_t>* rgba,
    std::vector<float>* inputTensor,
    std::string* error) {
    if (rgba == nullptr || inputTensor == nullptr) {
        if (error != nullptr) {
            *error = "Preprocessing output pointer is null";
        }
        return false;
    }
    if (capture.width <= 0 || capture.height <= 0) {
        if (error != nullptr) {
            *error = "Source dimensions must be positive";
        }
        return false;
    }
    if (!questlab::camera::ConvertYuv420ToRgba(capture, rgba)) {
        if (error != nullptr) {
            *error = "ConvertYuv420ToRgba rejected the fixture";
        }
        return false;
    }
    if (contract.input.shape.size() != 4U) {
        if (error != nullptr) {
            *error = "RF-DETR input shape must have four dimensions";
        }
        return false;
    }
    const int32_t targetHeight =
        static_cast<int32_t>(contract.input.shape[2]);
    const int32_t targetWidth =
        static_cast<int32_t>(contract.input.shape[3]);
    if (targetWidth <= 0 || targetHeight <= 0) {
        if (error != nullptr) {
            *error = "RF-DETR target dimensions must be positive";
        }
        return false;
    }
    const size_t targetPixels = static_cast<size_t>(targetWidth) *
                                static_cast<size_t>(targetHeight);
    if (targetPixels >
        std::numeric_limits<size_t>::max() / (3U * sizeof(float))) {
        if (error != nullptr) {
            *error = "RF-DETR input tensor size overflows";
        }
        return false;
    }
    inputTensor->assign(targetPixels * 3U, 0.0F);

    const float scaleX = static_cast<float>(capture.width) /
                         static_cast<float>(targetWidth);
    const float scaleY = static_cast<float>(capture.height) /
                         static_cast<float>(targetHeight);
    const size_t sourceWidth = static_cast<size_t>(capture.width);
    for (int32_t targetY = 0; targetY < targetHeight; ++targetY) {
        const float sourceY =
            (static_cast<float>(targetY) + 0.5F) * scaleY - 0.5F;
        const int32_t sourceY0Unclamped =
            static_cast<int32_t>(std::floor(sourceY));
        const int32_t sourceY1Unclamped = sourceY0Unclamped + 1;
        const int32_t sourceY0 = std::clamp(
            sourceY0Unclamped, 0, capture.height - 1);
        const int32_t sourceY1 = std::clamp(
            sourceY1Unclamped, 0, capture.height - 1);
        const float weightY =
            sourceY - static_cast<float>(sourceY0Unclamped);
        for (int32_t targetX = 0; targetX < targetWidth; ++targetX) {
            const float sourceX =
                (static_cast<float>(targetX) + 0.5F) * scaleX - 0.5F;
            const int32_t sourceX0Unclamped =
                static_cast<int32_t>(std::floor(sourceX));
            const int32_t sourceX1Unclamped = sourceX0Unclamped + 1;
            const int32_t sourceX0 = std::clamp(
                sourceX0Unclamped, 0, capture.width - 1);
            const int32_t sourceX1 = std::clamp(
                sourceX1Unclamped, 0, capture.width - 1);
            const float weightX =
                sourceX - static_cast<float>(sourceX0Unclamped);
            const size_t destination =
                static_cast<size_t>(targetY) *
                    static_cast<size_t>(targetWidth) +
                static_cast<size_t>(targetX);

            for (size_t channel = 0; channel < 3U; ++channel) {
                const auto sample = [&](int32_t y, int32_t x) {
                    const size_t offset =
                        (static_cast<size_t>(y) * sourceWidth +
                         static_cast<size_t>(x)) * 4U + channel;
                    return static_cast<float>((*rgba)[offset]);
                };
                const float top =
                    sample(sourceY0, sourceX0) * (1.0F - weightX) +
                    sample(sourceY0, sourceX1) * weightX;
                const float bottom =
                    sample(sourceY1, sourceX0) * (1.0F - weightX) +
                    sample(sourceY1, sourceX1) * weightX;
                const float pixel =
                    top * (1.0F - weightY) + bottom * weightY;
                (*inputTensor)[channel * targetPixels + destination] =
                    (pixel * contract.scale - contract.mean[channel]) /
                    contract.standardDeviation[channel];
            }
        }
    }
    return true;
}

}  // namespace questlab::rfdetr
