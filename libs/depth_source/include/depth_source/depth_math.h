#pragma once

#include "xr_math/xr_math.h"

#include <cstdint>

namespace questlab::depth {

// XR_META_environment_depth publishes a projected depth buffer, not metric
// distance. nearZ and farZ describe an OpenGL-convention projection, so a
// stored texel is 0 at the near plane and approaches 1 at the far plane, and
// must be linearized before it means anything in metres.
//
// farZ may be infinite. That is a normal report rather than an error: Meta's
// infinite projection stores 1 - nearZ/viewDepth, which is the limit of the
// finite expression and not a second convention.
struct DepthProjection {
    float nearZ = 0.0F;
    float farZ = 0.0F;
    // OpenXR field-of-view half-angles in radians. angleLeft and angleDown are
    // normally negative.
    float angleLeft = 0.0F;
    float angleRight = 0.0F;
    float angleUp = 0.0F;
    float angleDown = 0.0F;
};

// Returns the distance along the view -Z axis in metres. This is a planar
// depth, not a radial range: a texel at the edge of the image is further away
// than its linearized depth suggests, which is why UnprojectDepthTexel scales
// a full direction rather than multiplying a single axis.
bool LinearizeDepth(
    float ndcDepth,
    const DepthProjection& projection,
    float* metricDepth);

// Maps a texel centre to a view-space direction using the reported FOV.
// Coordinates are in texels; the caller supplies image dimensions so the
// mapping stays explicit rather than assuming a normalized input.
bool DepthTexelDirection(
    float texelX,
    float texelY,
    int32_t width,
    int32_t height,
    const DepthProjection& projection,
    math::Vec3* directionInView);

// Full texel to view-space metric point. Rejects non-finite input, depth
// outside the projection range, and the unreliable near field.
bool UnprojectDepthTexel(
    float texelX,
    float texelY,
    int32_t width,
    int32_t height,
    float ndcDepth,
    const DepthProjection& projection,
    math::Vec3* pointInView);

// Distance below which Meta's environment depth is not trustworthy. Samples
// closer than this are rejected rather than reported.
constexpr float kMinimumReliableDepthMeters = 0.2F;

}  // namespace questlab::depth
