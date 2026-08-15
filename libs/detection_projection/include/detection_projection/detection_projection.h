#pragma once

#include "camera_source/camera_source.h"
#include "xr_math/xr_math.h"

#include <array>
#include <cstdint>
#include <string>

namespace questlab::projection {

struct Pixel {
    float x = 0.0F;
    float y = 0.0F;
};

struct UnprojectionResult {
    // Android Camera2 calibration basis: +X right, +Y down, +Z forward.
    math::Vec3 directionInCamera{};
    float residualPixels = 0.0F;
};

// A monocular detection determines a bearing, not a range, so this record
// carries directions only. Metric position and extent require environment
// depth and arrive with the depth milestones. Nothing here may assume, dial
// in, or otherwise invent a distance: a box placed at a guessed range renders
// as a camera-facing billboard, which is a 2D overlay wearing a headset.
struct DetectionProjection {
    math::Vec3 originInLocal{};
    std::array<math::Vec3, 4> cornerDirectionsInLocal{};
    math::Vec3 centerDirectionInLocal{};
    float maximumResidualPixels = 0.0F;
};

// Android Camera2 reports Brown-Conrady coefficients as k1, k2, k3, p1,
// p2. Unprojection inverts that forward model with bounded Newton iterations
// and returns a ray in Camera2's +Z-forward calibration basis.
bool UnprojectPixel(
    const camera::CameraIntrinsics& intrinsics,
    const Pixel& pixel,
    UnprojectionResult* result,
    std::string* error);

bool ProjectDirection(
    const camera::CameraIntrinsics& intrinsics,
    const math::Vec3& directionInCamera,
    Pixel* pixel,
    std::string* error);

bool BuildDetectionProjection(
    const std::array<float, 4>& boxXyxy,
    int32_t imageWidth,
    int32_t imageHeight,
    const camera::CameraIntrinsics& intrinsics,
    const camera::CameraPose& cameraCalibration,
    const math::Pose& localFromHead,
    DetectionProjection* projection,
    std::string* error);

}  // namespace questlab::projection
