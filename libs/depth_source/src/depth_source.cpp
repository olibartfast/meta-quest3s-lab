#include "depth_source/depth_source.h"

#include <algorithm>
#include <cmath>

namespace questlab::depth {

void DepthCaptureToPoints(
    const DepthCapture& capture,
    int32_t sampleStride,
    float maximumRangeMeters,
    std::vector<math::Vec3>* pointsInSpace) {
    if (pointsInSpace == nullptr) {
        return;
    }
    pointsInSpace->clear();
    if (capture.width <= 0 || capture.height <= 0 ||
        capture.normalizedDepth.size() !=
            static_cast<size_t>(capture.width) *
                static_cast<size_t>(capture.height)) {
        return;
    }
    const int32_t stride = std::max(1, sampleStride);
    const size_t estimate =
        (static_cast<size_t>(capture.width / stride) + 1U) *
        (static_cast<size_t>(capture.height / stride) + 1U);
    pointsInSpace->reserve(estimate);

    for (int32_t y = 0; y < capture.height; y += stride) {
        for (int32_t x = 0; x < capture.width; x += stride) {
            const size_t index = static_cast<size_t>(y) *
                                     static_cast<size_t>(capture.width) +
                                 static_cast<size_t>(x);
            math::Vec3 pointInView{};
            if (!UnprojectDepthTexel(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    capture.width,
                    capture.height,
                    capture.normalizedDepth[index],
                    capture.view.projection,
                    &pointInView)) {
                continue;
            }
            if (math::Length(pointInView) > maximumRangeMeters) {
                continue;
            }
            pointsInSpace->push_back(
                math::TransformPoint(capture.view.poseInSpace, pointInView));
        }
    }
}

}  // namespace questlab::depth
