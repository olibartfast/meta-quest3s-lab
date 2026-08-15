#include "detection_projection/detection_projection.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

questlab::camera::CameraIntrinsics TestIntrinsics() {
    questlab::camera::CameraIntrinsics intrinsics;
    intrinsics.fx = 500.0F;
    intrinsics.fy = 510.0F;
    intrinsics.cx = 320.0F;
    intrinsics.cy = 240.0F;
    intrinsics.skew = 0.25F;
    intrinsics.distortion = {0.012F, -0.004F, 0.0005F, 0.0003F, -0.0002F};
    intrinsics.valid = true;
    return intrinsics;
}

}  // namespace

int main() {
    bool passed = true;
    const auto intrinsics = TestIntrinsics();
    const questlab::projection::Pixel source{511.25F, 173.5F};
    questlab::projection::UnprojectionResult unprojected;
    std::string error;
    passed &= Expect(
        questlab::projection::UnprojectPixel(
            intrinsics, source, &unprojected, &error),
        error.c_str());
    questlab::projection::Pixel roundTrip;
    passed &= Expect(
        questlab::projection::ProjectDirection(
            intrinsics,
            unprojected.directionInCamera,
            &roundTrip,
            &error),
        error.c_str());
    passed &= Expect(
        std::fabs(roundTrip.x - source.x) < 0.01F &&
            std::fabs(roundTrip.y - source.y) < 0.01F,
        "distorted pixel round trip must be below 0.01 pixel");

    questlab::camera::CameraPose cameraCalibration;
    cameraCalibration.position = {0.1F, -0.05F, 0.02F};
    cameraCalibration.valid = true;
    questlab::math::Pose localFromHead;
    localFromHead.position = {1.0F, 2.0F, 3.0F};
    questlab::projection::DetectionProjection projection;
    passed &= Expect(
        questlab::projection::BuildDetectionProjection(
            {250.0F, 180.0F, 390.0F, 300.0F},
            640,
            480,
            intrinsics,
            cameraCalibration,
            localFromHead,
            &projection,
            &error),
        error.c_str());
    passed &= Expect(
        questlab::math::NearlyEqual(
            projection.originInLocal,
            {1.1F, 1.95F, 3.02F},
            1.0e-5F),
        "camera origin must compose into LOCAL");
    {
        bool directionsUnit = true;
        bool directionsDistinct = true;
        for (size_t i = 0; i < projection.cornerDirectionsInLocal.size(); ++i) {
            directionsUnit &= std::fabs(questlab::math::Length(
                projection.cornerDirectionsInLocal[i]) - 1.0F) < 1.0e-4F;
            for (size_t j = i + 1;
                 j < projection.cornerDirectionsInLocal.size(); ++j) {
                directionsDistinct &= !questlab::math::NearlyEqual(
                    projection.cornerDirectionsInLocal[i],
                    projection.cornerDirectionsInLocal[j],
                    1.0e-4F);
            }
        }
        passed &= Expect(
            directionsUnit, "corner bearings must be unit length");
        passed &= Expect(
            directionsDistinct, "corner bearings must be distinct");
        passed &= Expect(
            std::fabs(questlab::math::Length(
                projection.centerDirectionInLocal) - 1.0F) < 1.0e-4F,
            "centre bearing must be unit length");
    }
    passed &= Expect(
        projection.maximumResidualPixels < 0.01F,
        "distortion inversion residual must be measured and bounded");

    // A forward-facing Quest camera sees along OpenXR head -Z. Camera2's
    // optical ray is +Z, while LENS_POSE_ROTATION maps head -Z to camera +Z.
    // Applying that quaternion without inversion puts the detection behind the
    // user, which this regression test prevents.
    cameraCalibration.orientation = {1.0F, 0.0F, 0.0F, 0.0F};
    cameraCalibration.position = {0.0F, 0.0F, 0.0F};
    questlab::math::Pose identityHead;
    passed &= Expect(
        questlab::projection::BuildDetectionProjection(
            {300.0F, 220.0F, 340.0F, 260.0F},
            640,
            480,
            intrinsics,
            cameraCalibration,
            identityHead,
            &projection,
            &error),
        error.c_str());
    passed &= Expect(
        questlab::math::NearlyEqual(
            projection.centerDirectionInLocal,
            {0.0F, 0.0F, -1.0F},
            1.0e-5F),
        "Camera2 pose rotation must be inverted into head space");
    return passed ? 0 : 1;
}
