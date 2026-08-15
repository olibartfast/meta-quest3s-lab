#include "detection_fusion/detection_fusion.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

// A frustum looking down LOCAL -Z from the origin, half-angle ~11 degrees.
questlab::fusion::DetectionFrustum MakeFrustum(float halfTangent) {
    questlab::fusion::DetectionFrustum frustum;
    frustum.originInLocal = {0.0F, 0.0F, 0.0F};
    frustum.centerDirectionInLocal = {0.0F, 0.0F, -1.0F};
    const std::array<std::pair<float, float>, 4> corners = {{
        {-halfTangent, halfTangent},
        {halfTangent, halfTangent},
        {halfTangent, -halfTangent},
        {-halfTangent, -halfTangent},
    }};
    for (size_t i = 0; i < 4; ++i) {
        questlab::math::Vec3 direction{
            corners[i].first, corners[i].second, -1.0F};
        questlab::math::Normalize(&direction);
        frustum.cornerDirectionsInLocal[i] = direction;
    }
    return frustum;
}

// A flat slab of samples at a given distance, spanning halfWidth/halfHeight.
void AddSlab(
    std::vector<questlab::math::Vec3>* points,
    float distance,
    float halfWidth,
    float halfHeight,
    int steps) {
    for (int ix = -steps; ix <= steps; ++ix) {
        for (int iy = -steps; iy <= steps; ++iy) {
            const float fx = static_cast<float>(ix) / static_cast<float>(steps);
            const float fy = static_cast<float>(iy) / static_cast<float>(steps);
            points->push_back(
                {fx * halfWidth, fy * halfHeight, -distance});
        }
    }
}

}  // namespace

int main() {
    bool passed = true;
    const auto frustum = MakeFrustum(0.2F);
    questlab::fusion::FusionParameters parameters;

    // A single slab at 2 m must produce a box centred on it.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.20F, 10);
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "slab fusion must succeed" : reason.c_str());
        passed &= Expect(
            std::fabs(box.centerInLocal.z + 2.0F) < 0.05F,
            "box centre must sit at the slab distance");
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 2.0F) < 0.15F,
            "representative range must match the slab");
        // Percentile extents trim the outer 5%, so expect slightly under the
        // true half-width rather than exactly it.
        passed &= Expect(
            box.halfExtents.x > 0.10F && box.halfExtents.x < 0.16F,
            "half-width must approximate the slab half-width");
        passed &= Expect(
            box.halfExtents.y > 0.14F && box.halfExtents.y < 0.21F,
            "half-height must approximate the slab half-height");
    }

    // Object in front of a wall: the near surface must win, and the wall must
    // not drag the centre backwards. This is the case a 2D box always creates.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 1.5F, 0.10F, 0.10F, 8);   // object
        AddSlab(&points, 4.0F, 0.60F, 0.60F, 12);  // wall behind
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "object-in-front fusion must succeed"
                           : reason.c_str());
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 1.5F) < 0.2F,
            "the nearer surface must be selected over the wall");
        passed &= Expect(
            box.centerInLocal.z > -2.0F,
            "the wall must not pull the centre backwards");
    }

    // Worst case: the wall is far denser than the object. The object must
    // still win, and the inlier-ratio floor must not then refuse it.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 1.2F, 0.08F, 0.08F, 6);   // small object, 169 pts
        AddSlab(&points, 3.5F, 0.55F, 0.55F, 20);  // dense wall, 1681 pts
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            reason.empty() ? "dense-wall fusion must succeed" : reason.c_str());
        passed &= Expect(
            std::fabs(box.representativeRangeMeters - 1.2F) < 0.2F,
            "a small near object must beat a dense far wall");
        passed &= Expect(
            box.inlierRatio < 0.3F,
            "this case must genuinely have a low inlier ratio");
    }

    // Points outside the frustum must be ignored entirely.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.15F, 10);
        // A dense slab well outside the cone, laterally offset.
        for (int i = 0; i < 4000; ++i) {
            points.push_back({5.0F, 0.0F, -2.0F});
        }
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            "fusion must succeed while ignoring outside points");
        passed &= Expect(
            box.halfExtents.x < 0.3F,
            "points outside the frustum must not widen the box");
        passed &= Expect(
            !questlab::fusion::PointInsideFrustum(
                frustum, {5.0F, 0.0F, -2.0F}, parameters.borderInsetFraction),
            "a laterally distant point must be outside the frustum");
        passed &= Expect(
            questlab::fusion::PointInsideFrustum(
                frustum, {0.0F, 0.0F, -2.0F}, parameters.borderInsetFraction),
            "an axial point must be inside the frustum");
    }

    // Behind the camera is never inside, whatever the lateral position.
    passed &= Expect(
        !questlab::fusion::PointInsideFrustum(
            frustum, {0.0F, 0.0F, 2.0F}, parameters.borderInsetFraction),
        "a point behind the camera must be rejected");

    // Too few samples is a refusal with a reason, not a tiny box.
    {
        std::vector<questlab::math::Vec3> points;
        points.push_back({0.0F, 0.0F, -2.0F});
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &box, &reason),
            "sparse evidence must be refused");
        passed &= Expect(!reason.empty(), "a refusal must carry a reason");
    }

    // Empty input must refuse rather than produce a zero box at the origin.
    {
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            !questlab::fusion::FuseDetection(
                frustum, {}, parameters, {}, &box, &reason),
            "empty depth must be refused");
    }

    // The box must be level with the room: its up axis follows LOCAL up even
    // though the bearing is tilted downward.
    {
        questlab::fusion::DetectionFrustum tilted = MakeFrustum(0.2F);
        questlab::math::Vec3 bearing{0.0F, -0.5F, -1.0F};
        questlab::math::Normalize(&bearing);
        tilted.centerDirectionInLocal = bearing;
        for (auto& corner : tilted.cornerDirectionsInLocal) {
            corner = questlab::math::Add(corner, {0.0F, -0.5F, 0.0F});
            questlab::math::Normalize(&corner);
        }
        std::vector<questlab::math::Vec3> points;
        for (int ix = -8; ix <= 8; ++ix) {
            for (int iy = -8; iy <= 8; ++iy) {
                const float fx = static_cast<float>(ix) / 8.0F;
                const float fy = static_cast<float>(iy) / 8.0F;
                points.push_back(questlab::math::Add(
                    questlab::math::Scale(bearing, 2.0F),
                    {fx * 0.12F, fy * 0.12F, 0.0F}));
            }
        }
        questlab::fusion::FusedBox box;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                tilted, points, parameters, {}, &box, &reason),
            reason.empty() ? "tilted fusion must succeed" : reason.c_str());
        const questlab::math::Vec3 boxUp =
            questlab::math::Rotate(box.orientationInLocal, {0.0F, 1.0F, 0.0F});
        passed &= Expect(
            std::fabs(boxUp.y - 1.0F) < 0.05F,
            "the box up axis must stay aligned with LOCAL up");
    }

    // With a size prior the near face must stay where depth measured it and
    // the box must grow away from the viewer, not straddle the surface.
    {
        std::vector<questlab::math::Vec3> points;
        AddSlab(&points, 2.0F, 0.15F, 0.15F, 10);
        questlab::fusion::FusedBox measured;
        questlab::fusion::FusedBox prior;
        std::string reason;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, {}, &measured, &reason),
            "measured fusion must succeed");
        questlab::fusion::ObjectSizePrior sizePrior;
        sizePrior.valid = true;
        sizePrior.depthMeters = 0.60F;
        passed &= Expect(
            questlab::fusion::FuseDetection(
                frustum, points, parameters, sizePrior, &prior, &reason),
            "prior fusion must succeed");
        passed &= Expect(
            !measured.depthExtentIsAssumed && prior.depthExtentIsAssumed,
            "only the prior result may report an assumed depth extent");
        passed &= Expect(
            std::fabs(prior.halfExtents.z - 0.30F) < 1.0e-3F,
            "the prior must set the half depth");
        // forward is away from the viewer, i.e. -Z here, so the centre must
        // move further from the origin than the measured slab.
        passed &= Expect(
            prior.centerInLocal.z < measured.centerInLocal.z,
            "the prior box must extend away from the viewer");
        passed &= Expect(
            std::fabs(prior.centerInLocal.z + 2.0F + 0.30F) < 0.05F,
            "the near face must stay on the measured surface");
        passed &= Expect(
            std::fabs(prior.halfExtents.x - measured.halfExtents.x) < 1.0e-4F,
            "lateral extents must remain measured");
    }

    if (passed) {
        std::cout << "detection fusion tests passed\n";
    }
    return passed ? 0 : 1;
}
