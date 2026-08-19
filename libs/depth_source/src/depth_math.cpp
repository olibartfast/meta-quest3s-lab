#include "depth_source/depth_math.h"

#include <cmath>

namespace questlab::depth {
namespace {

bool ProjectionUsable(const DepthProjection& projection) {
    if (!std::isfinite(projection.nearZ) || projection.nearZ <= 0.0F) {
        return false;
    }
    // An infinite farZ is valid. A finite one must sit beyond nearZ.
    if (!std::isnan(projection.farZ) && projection.farZ <= projection.nearZ) {
        return false;
    }
    return !std::isnan(projection.farZ);
}

}  // namespace

bool LinearizeDepth(
    float ndcDepth,
    const DepthProjection& projection,
    float* metricDepth) {
    if (metricDepth == nullptr || !ProjectionUsable(projection) ||
        !std::isfinite(ndcDepth) || ndcDepth < 0.0F || ndcDepth >= 1.0F) {
        return false;
    }

    float linear = 0.0F;
    if (std::isinf(projection.farZ)) {
        // Meta's infinite projection puts [-1, -2*nearZ; -1, 0] in the
        // bottom-right quadrant, which gives a stored depth of
        // 1 - nearZ/viewDepth. This is the limit of the finite expression
        // below as farZ grows, not a different convention.
        linear = projection.nearZ / (1.0F - ndcDepth);
    } else {
        const float denominator =
            projection.farZ +
            ndcDepth * (projection.nearZ - projection.farZ);
        if (std::fabs(denominator) < 1.0e-9F) {
            return false;
        }
        linear = (projection.nearZ * projection.farZ) / denominator;
    }

    if (!std::isfinite(linear) || linear <= 0.0F) {
        return false;
    }
    *metricDepth = linear;
    return true;
}

bool DepthTexelDirection(
    float texelX,
    float texelY,
    int32_t width,
    int32_t height,
    const DepthProjection& projection,
    math::Vec3* directionInView) {
    if (directionInView == nullptr || width <= 0 || height <= 0 ||
        !std::isfinite(texelX) || !std::isfinite(texelY)) {
        return false;
    }
    if (!std::isfinite(projection.angleLeft) ||
        !std::isfinite(projection.angleRight) ||
        !std::isfinite(projection.angleUp) ||
        !std::isfinite(projection.angleDown)) {
        return false;
    }
    if (projection.angleRight <= projection.angleLeft ||
        projection.angleUp <= projection.angleDown) {
        return false;
    }

    // Texel centres, so 0.5 offsets. u runs left to right, v runs top to
    // bottom, which is why the vertical term interpolates from angleUp down.
    const float u = (texelX + 0.5F) / static_cast<float>(width);
    const float v = (texelY + 0.5F) / static_cast<float>(height);
    if (u < 0.0F || u > 1.0F || v < 0.0F || v > 1.0F) {
        return false;
    }

    const float tanLeft = std::tan(projection.angleLeft);
    const float tanRight = std::tan(projection.angleRight);
    const float tanUp = std::tan(projection.angleUp);
    const float tanDown = std::tan(projection.angleDown);

    const float x = tanLeft + u * (tanRight - tanLeft);
    const float y = tanUp + v * (tanDown - tanUp);

    // OpenXR view space looks along -Z, so a unit step of depth is -1 there
    // and the tangents scale directly.
    math::Vec3 direction{x, y, -1.0F};
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y)) {
        return false;
    }
    *directionInView = direction;
    return true;
}

bool UnprojectDepthTexel(
    float texelX,
    float texelY,
    int32_t width,
    int32_t height,
    float ndcDepth,
    const DepthProjection& projection,
    math::Vec3* pointInView) {
    if (pointInView == nullptr) {
        return false;
    }
    float metricDepth = 0.0F;
    if (!LinearizeDepth(ndcDepth, projection, &metricDepth)) {
        return false;
    }
    if (metricDepth < kMinimumReliableDepthMeters) {
        return false;
    }
    math::Vec3 direction{};
    if (!DepthTexelDirection(
            texelX, texelY, width, height, projection, &direction)) {
        return false;
    }
    // direction.z is -1, so scaling by the planar depth places the point at
    // exactly that distance along -Z while preserving the lateral offsets.
    *pointInView = math::Scale(direction, metricDepth);
    return std::isfinite(pointInView->x) && std::isfinite(pointInView->y) &&
           std::isfinite(pointInView->z);
}

}  // namespace questlab::depth
