#include "detection_projection/detection_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace questlab::projection {
namespace {

constexpr int32_t kMaximumNewtonIterations = 12;
constexpr float kNormalizedResidualTolerance = 1.0e-7F;
constexpr float kJacobianStep = 1.0e-4F;

bool IsFinite(const math::Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

Pixel DistortNormalized(
    const camera::CameraIntrinsics& intrinsics,
    float x,
    float y) {
    const float radius2 = x * x + y * y;
    const float radius4 = radius2 * radius2;
    const float radius6 = radius4 * radius2;
    const float radial =
        1.0F + intrinsics.distortion[0] * radius2 +
        intrinsics.distortion[1] * radius4 +
        intrinsics.distortion[2] * radius6;
    const float p1 = intrinsics.distortion[3];
    const float p2 = intrinsics.distortion[4];
    return {
        x * radial + 2.0F * p1 * x * y +
            p2 * (radius2 + 2.0F * x * x),
        y * radial + p1 * (radius2 + 2.0F * y * y) +
            2.0F * p2 * x * y,
    };
}

bool ValidateIntrinsics(
    const camera::CameraIntrinsics& intrinsics,
    std::string* error) {
    if (!intrinsics.valid || !std::isfinite(intrinsics.fx) ||
        !std::isfinite(intrinsics.fy) || intrinsics.fx <= 0.0F ||
        intrinsics.fy <= 0.0F) {
        if (error != nullptr) {
            *error = "Camera intrinsics are unavailable or invalid";
        }
        return false;
    }
    for (float coefficient : intrinsics.distortion) {
        if (!std::isfinite(coefficient)) {
            if (error != nullptr) {
                *error = "Camera distortion contains a non-finite value";
            }
            return false;
        }
    }
    return true;
}

}  // namespace

bool UnprojectPixel(
    const camera::CameraIntrinsics& intrinsics,
    const Pixel& pixel,
    UnprojectionResult* result,
    std::string* error) {
    if (result == nullptr || !ValidateIntrinsics(intrinsics, error) ||
        !std::isfinite(pixel.x) || !std::isfinite(pixel.y)) {
        if (result == nullptr && error != nullptr) {
            *error = "Unprojection output pointer is null";
        }
        return false;
    }

    const float distortedY = (pixel.y - intrinsics.cy) / intrinsics.fy;
    const float distortedX =
        (pixel.x - intrinsics.cx - intrinsics.skew * distortedY) /
        intrinsics.fx;
    float x = distortedX;
    float y = distortedY;
    for (int32_t iteration = 0;
         iteration < kMaximumNewtonIterations;
         ++iteration) {
        const Pixel predicted = DistortNormalized(intrinsics, x, y);
        const float errorX = predicted.x - distortedX;
        const float errorY = predicted.y - distortedY;
        if (std::max(std::fabs(errorX), std::fabs(errorY)) <=
            kNormalizedResidualTolerance) {
            break;
        }
        const Pixel xStep =
            DistortNormalized(intrinsics, x + kJacobianStep, y);
        const Pixel yStep =
            DistortNormalized(intrinsics, x, y + kJacobianStep);
        const float j00 = (xStep.x - predicted.x) / kJacobianStep;
        const float j10 = (xStep.y - predicted.y) / kJacobianStep;
        const float j01 = (yStep.x - predicted.x) / kJacobianStep;
        const float j11 = (yStep.y - predicted.y) / kJacobianStep;
        const float determinant = j00 * j11 - j01 * j10;
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-9F) {
            if (error != nullptr) {
                *error = "Distortion inversion reached a singular Jacobian";
            }
            return false;
        }
        x -= (j11 * errorX - j01 * errorY) / determinant;
        y -= (-j10 * errorX + j00 * errorY) / determinant;
        if (!std::isfinite(x) || !std::isfinite(y)) {
            if (error != nullptr) {
                *error = "Distortion inversion diverged";
            }
            return false;
        }
    }

    const Pixel finalPrediction = DistortNormalized(intrinsics, x, y);
    const float residualX =
        (finalPrediction.x - distortedX) * intrinsics.fx;
    const float residualY =
        (finalPrediction.y - distortedY) * intrinsics.fy;
    result->residualPixels = std::sqrt(
        residualX * residualX + residualY * residualY);
    // Camera2 calibration coordinates are +X right, +Y down, +Z forward.
    // LENS_POSE_ROTATION maps the Android sensor/head basis into this basis,
    // so leave the ray in Camera2 coordinates until that rotation is inverted.
    result->directionInCamera = {x, y, 1.0F};
    if (!math::Normalize(&result->directionInCamera) ||
        !std::isfinite(result->residualPixels) ||
        result->residualPixels > 0.05F) {
        if (error != nullptr) {
            *error = "Distortion inversion residual exceeds 0.05 pixels";
        }
        return false;
    }
    return true;
}

bool ProjectDirection(
    const camera::CameraIntrinsics& intrinsics,
    const math::Vec3& directionInCamera,
    Pixel* pixel,
    std::string* error) {
    if (pixel == nullptr || !ValidateIntrinsics(intrinsics, error) ||
        !IsFinite(directionInCamera) || directionInCamera.z <= 1.0e-6F) {
        if (pixel == nullptr && error != nullptr) {
            *error = "Projection output pointer is null";
        } else if (error != nullptr && directionInCamera.z <= 1.0e-6F) {
            *error = "Direction does not point through the camera image plane";
        }
        return false;
    }
    const float x = directionInCamera.x / directionInCamera.z;
    const float y = directionInCamera.y / directionInCamera.z;
    const Pixel distorted = DistortNormalized(intrinsics, x, y);
    pixel->x = intrinsics.fx * distorted.x +
               intrinsics.skew * distorted.y + intrinsics.cx;
    pixel->y = intrinsics.fy * distorted.y + intrinsics.cy;
    return std::isfinite(pixel->x) && std::isfinite(pixel->y);
}

bool BuildDetectionProjection(
    const std::array<float, 4>& boxXyxy,
    int32_t imageWidth,
    int32_t imageHeight,
    const camera::CameraIntrinsics& intrinsics,
    const camera::CameraPose& cameraCalibration,
    const math::Pose& localFromHead,
    DetectionProjection* projection,
    std::string* error) {
    if (projection == nullptr || imageWidth <= 0 || imageHeight <= 0 ||
        !cameraCalibration.valid) {
        if (error != nullptr) {
            *error = "Detection projection input is invalid";
        }
        return false;
    }
    if (boxXyxy[0] < 0.0F || boxXyxy[1] < 0.0F ||
        boxXyxy[2] > static_cast<float>(imageWidth) ||
        boxXyxy[3] > static_cast<float>(imageHeight) ||
        boxXyxy[2] <= boxXyxy[0] || boxXyxy[3] <= boxXyxy[1]) {
        if (error != nullptr) {
            *error = "Detection box is outside the valid camera image";
        }
        return false;
    }

    const std::array<Pixel, 5> pixels = {{
        {boxXyxy[0], boxXyxy[1]},
        {boxXyxy[2], boxXyxy[1]},
        {boxXyxy[2], boxXyxy[3]},
        {boxXyxy[0], boxXyxy[3]},
        {
            (boxXyxy[0] + boxXyxy[2]) * 0.5F,
            (boxXyxy[1] + boxXyxy[3]) * 0.5F,
        },
    }};
    // Android's LENS_POSE_ROTATION is cameraFromHead: it transforms a vector
    // from the Android sensor/head axes into the Camera2 optical axes. Rays
    // need the inverse direction, headFromCamera. LENS_POSE_TRANSLATION is
    // already the optical-center position in the sensor/head axes; it is not
    // the translation component of cameraFromHead's projection matrix.
    math::Quat cameraFromHeadRotation{
        cameraCalibration.orientation[0],
        cameraCalibration.orientation[1],
        cameraCalibration.orientation[2],
        cameraCalibration.orientation[3],
    };
    if (!math::Normalize(&cameraFromHeadRotation)) {
        if (error != nullptr) {
            *error = "Camera pose orientation is invalid";
        }
        return false;
    }
    const math::Quat headFromCameraRotation =
        math::Conjugate(cameraFromHeadRotation);
    const math::Vec3 cameraOriginInHead{
        cameraCalibration.position[0],
        cameraCalibration.position[1],
        cameraCalibration.position[2],
    };
    math::Pose normalizedLocalFromHead = localFromHead;
    if (!math::Normalize(&normalizedLocalFromHead.orientation)) {
        if (error != nullptr) {
            *error = "Head pose orientation is invalid";
        }
        return false;
    }
    const math::Vec3 cameraOriginInLocal = math::TransformPoint(
        normalizedLocalFromHead, cameraOriginInHead);

    std::array<math::Vec3, 5> directions{};
    float maximumResidual = 0.0F;
    for (size_t index = 0; index < pixels.size(); ++index) {
        UnprojectionResult unprojected;
        if (!UnprojectPixel(intrinsics, pixels[index], &unprojected, error)) {
            return false;
        }
        const math::Vec3 directionInHead = math::Rotate(
            headFromCameraRotation, unprojected.directionInCamera);
        directions[index] = math::TransformDirection(
            normalizedLocalFromHead, directionInHead);
        if (!math::Normalize(&directions[index])) {
            if (error != nullptr) {
                *error = "Projected LOCAL direction is invalid";
            }
            return false;
        }
        maximumResidual =
            std::max(maximumResidual, unprojected.residualPixels);
    }

    DetectionProjection result;
    result.originInLocal = cameraOriginInLocal;
    std::copy_n(
        directions.begin(),
        result.cornerDirectionsInLocal.size(),
        result.cornerDirectionsInLocal.begin());
    result.centerDirectionInLocal = directions[4];
    result.maximumResidualPixels = maximumResidual;
    *projection = result;
    return true;
}

}  // namespace questlab::projection
