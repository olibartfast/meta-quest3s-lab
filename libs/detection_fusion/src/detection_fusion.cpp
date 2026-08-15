#include "detection_fusion/detection_fusion.h"

#include <algorithm>
#include <cmath>

namespace questlab::fusion {
namespace {

// Inward normals of the four side planes of the pyramid. Each is built from
// two adjacent corner rays, so the test needs no projection back into image
// space and works entirely in LOCAL.
bool BuildInwardNormals(
    const DetectionFrustum& frustum,
    float borderInsetFraction,
    std::array<math::Vec3, 4>* normals) {
    std::array<math::Vec3, 4> corners = frustum.cornerDirectionsInLocal;
    const float inset = std::clamp(borderInsetFraction, 0.0F, 0.45F);
    if (inset > 0.0F) {
        // Shrink each corner ray toward the centre bearing.
        for (math::Vec3& corner : corners) {
            corner = math::Add(
                math::Scale(corner, 1.0F - inset),
                math::Scale(frustum.centerDirectionInLocal, inset));
            if (!math::Normalize(&corner)) {
                return false;
            }
        }
    }

    for (size_t index = 0; index < corners.size(); ++index) {
        const math::Vec3& current = corners[index];
        const math::Vec3& next = corners[(index + 1) % corners.size()];
        math::Vec3 normal = math::Cross(current, next);
        if (!math::Normalize(&normal)) {
            return false;
        }
        // Orient the normal so the centre bearing is on its positive side.
        if (math::Dot(normal, frustum.centerDirectionInLocal) < 0.0F) {
            normal = math::Scale(normal, -1.0F);
        }
        (*normals)[index] = normal;
    }
    return true;
}

// Deterministic pseudo-random index source. A fixed sequence keeps fusion
// reproducible frame to frame, which matters because a box that jitters
// because of RANSAC sampling is indistinguishable from a box that jitters
// because the sensor moved.
struct SampleSequence {
    uint32_t state = 0x9E3779B9U;
    uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

// Fits a plane by consensus and returns its unit normal and the inlier set.
bool FitPlaneRansac(
    const std::vector<math::Vec3>& points,
    const FusionParameters& parameters,
    math::Vec3* normal,
    std::vector<math::Vec3>* inliers) {
    if (points.size() < 3) {
        return false;
    }
    SampleSequence sequence;
    size_t bestCount = 0;
    math::Vec3 bestNormal{};
    float bestOffset = 0.0F;
    for (uint32_t iteration = 0; iteration < parameters.ransacIterations;
         ++iteration) {
        const math::Vec3& a = points[sequence.Next() % points.size()];
        const math::Vec3& b = points[sequence.Next() % points.size()];
        const math::Vec3& c = points[sequence.Next() % points.size()];
        math::Vec3 candidate =
            math::Cross(math::Subtract(b, a), math::Subtract(c, a));
        if (!math::Normalize(&candidate)) {
            continue;
        }
        const float offset = math::Dot(candidate, a);
        size_t count = 0;
        for (const math::Vec3& point : points) {
            if (std::fabs(math::Dot(candidate, point) - offset) <=
                parameters.ransacInlierMeters) {
                ++count;
            }
        }
        if (count > bestCount) {
            bestCount = count;
            bestNormal = candidate;
            bestOffset = offset;
        }
    }
    const float ratio =
        static_cast<float>(bestCount) / static_cast<float>(points.size());
    if (ratio < parameters.ransacMinimumInlierRatio) {
        return false;
    }
    inliers->clear();
    inliers->reserve(bestCount);
    for (const math::Vec3& point : points) {
        if (std::fabs(math::Dot(bestNormal, point) - bestOffset) <=
            parameters.ransacInlierMeters) {
            inliers->push_back(point);
        }
    }
    *normal = bestNormal;
    return true;
}

float Percentile(std::vector<float>* values, float fraction) {
    if (values->empty()) {
        return 0.0F;
    }
    const float clamped = std::clamp(fraction, 0.0F, 1.0F);
    const size_t index = static_cast<size_t>(std::llround(
        clamped * static_cast<float>(values->size() - 1)));
    std::nth_element(values->begin(), values->begin() + index, values->end());
    return (*values)[index];
}

}  // namespace

bool PointInsideFrustum(
    const DetectionFrustum& frustum,
    const math::Vec3& pointInLocal,
    float borderInsetFraction) {
    std::array<math::Vec3, 4> normals{};
    if (!BuildInwardNormals(frustum, borderInsetFraction, &normals)) {
        return false;
    }
    const math::Vec3 offset =
        math::Subtract(pointInLocal, frustum.originInLocal);
    // Behind the camera is never inside.
    if (math::Dot(offset, frustum.centerDirectionInLocal) <= 0.0F) {
        return false;
    }
    for (const math::Vec3& normal : normals) {
        if (math::Dot(offset, normal) < 0.0F) {
            return false;
        }
    }
    return true;
}

bool FuseDetection(
    const DetectionFrustum& frustum,
    const std::vector<math::Vec3>& depthPointsInLocal,
    const FusionParameters& parameters,
    const ObjectSizePrior& sizePrior,
    FusedBox* box,
    std::string* reason) {
    const auto fail = [&](const char* message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (box == nullptr) {
        return fail("Output box is null");
    }
    if (parameters.clusterToleranceMeters <= 0.0F ||
        parameters.maximumRangeMeters <= parameters.minimumRangeMeters) {
        return fail("Fusion parameters are invalid");
    }

    std::array<math::Vec3, 4> normals{};
    if (!BuildInwardNormals(
            frustum, parameters.borderInsetFraction, &normals)) {
        return fail("Detection frustum is degenerate");
    }

    // Pass one: keep points inside the frustum and within the working range,
    // recording the range along the centre bearing for clustering.
    std::vector<math::Vec3> inside;
    std::vector<float> ranges;
    inside.reserve(depthPointsInLocal.size());
    ranges.reserve(depthPointsInLocal.size());
    for (const math::Vec3& point : depthPointsInLocal) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const math::Vec3 offset =
            math::Subtract(point, frustum.originInLocal);
        const float axial = math::Dot(offset, frustum.centerDirectionInLocal);
        if (axial < parameters.minimumRangeMeters ||
            axial > parameters.maximumRangeMeters) {
            continue;
        }
        bool insideAll = true;
        for (const math::Vec3& normal : normals) {
            if (math::Dot(offset, normal) < 0.0F) {
                insideAll = false;
                break;
            }
        }
        if (!insideAll) {
            continue;
        }
        inside.push_back(point);
        ranges.push_back(axial);
    }

    if (inside.size() < parameters.minimumSampleCount) {
        return fail("Too few depth samples inside the detection");
    }

    // Pass two: find the nearest surface with enough support, not the most
    // populated one. A wall behind the object usually yields more samples
    // than the object itself, so selecting the largest cluster would place
    // the box on the wall. What matters is occlusion order: inside a
    // detection's frustum, the first substantial surface is the object.
    std::vector<float> sortedRanges = ranges;
    std::sort(sortedRanges.begin(), sortedRanges.end());
    const float window = parameters.clusterToleranceMeters * 2.0F;
    const size_t supportThreshold = std::max<size_t>(
        parameters.minimumSampleCount,
        static_cast<size_t>(std::llround(
            static_cast<double>(parameters.foregroundSampleShare) *
            static_cast<double>(sortedRanges.size()))));

    bool haveCluster = false;
    float clusterCentre = 0.0F;
    size_t high = 0;
    for (size_t low = 0; low < sortedRanges.size(); ++low) {
        if (high < low) {
            high = low;
        }
        while (high + 1 < sortedRanges.size() &&
               sortedRanges[high + 1] - sortedRanges[low] <= window) {
            ++high;
        }
        const size_t count = high - low + 1;
        if (count >= supportThreshold) {
            // Median of the window is more stable than its midpoint when the
            // surface is not flat.
            clusterCentre = sortedRanges[low + count / 2];
            haveCluster = true;
            break;
        }
    }
    if (!haveCluster) {
        return fail("No depth surface with enough support");
    }

    // Pass three: keep the cluster's inliers.
    std::vector<math::Vec3> cluster;  // NOLINT: replaced by plane inliers
    cluster.reserve(inside.size());
    for (size_t index = 0; index < inside.size(); ++index) {
        if (std::fabs(ranges[index] - clusterCentre) <=
            parameters.clusterToleranceMeters) {
            cluster.push_back(inside[index]);
        }
    }
    const float inlierRatio = static_cast<float>(cluster.size()) /
                              static_cast<float>(inside.size());
    if (cluster.size() < parameters.minimumSampleCount) {
        return fail("Dominant cluster is too small");
    }
    if (inlierRatio < parameters.minimumInlierRatio) {
        return fail("Dominant cluster is not dominant enough");
    }

    // Orientation: yaw about the up axis, taken from the centre bearing
    // flattened onto the horizontal plane. Depth from a single viewpoint sees
    // only the front surface, so a PCA yaw would fit the visible face rather
    // than the object; facing the box along the viewing bearing is the honest
    // convention and is documented as such.
    math::Vec3 up = parameters.upInLocal;
    if (!math::Normalize(&up)) {
        return fail("Up axis is degenerate");
    }
    // Prefer the plane normal: it is a measured orientation. Fall back to the
    // viewing bearing only when no plane reaches consensus.
    math::Vec3 forward = frustum.centerDirectionInLocal;
    math::Vec3 planeNormal{};
    std::vector<math::Vec3> planeInliers;
    bool orientationMeasured = false;
    if (FitPlaneRansac(cluster, parameters, &planeNormal, &planeInliers)) {
        // Point the normal away from the viewer so the box grows backwards.
        if (math::Dot(planeNormal, frustum.centerDirectionInLocal) < 0.0F) {
            planeNormal = math::Scale(planeNormal, -1.0F);
        }
        forward = planeNormal;
        orientationMeasured = true;
        cluster = planeInliers;
    }
    if (parameters.requireMeasuredOrientation && !orientationMeasured) {
        return fail("No planar surface reached consensus");
    }
    forward = math::Subtract(forward, math::Scale(up, math::Dot(forward, up)));
    if (!math::Normalize(&forward)) {
        return fail("Bearing is parallel to the up axis");
    }
    math::Vec3 right = math::Cross(forward, up);
    if (!math::Normalize(&right)) {
        return fail("Cannot build a box basis");
    }

    // Extents in the box basis, from percentiles.
    std::vector<float> alongRight;
    std::vector<float> alongUp;
    std::vector<float> alongForward;
    alongRight.reserve(cluster.size());
    alongUp.reserve(cluster.size());
    alongForward.reserve(cluster.size());
    for (const math::Vec3& point : cluster) {
        alongRight.push_back(math::Dot(point, right));
        alongUp.push_back(math::Dot(point, up));
        alongForward.push_back(math::Dot(point, forward));
    }

    const float rightLow =
        Percentile(&alongRight, parameters.extentLowPercentile);
    const float rightHigh =
        Percentile(&alongRight, parameters.extentHighPercentile);
    const float upLow = Percentile(&alongUp, parameters.extentLowPercentile);
    const float upHigh = Percentile(&alongUp, parameters.extentHighPercentile);
    const float forwardLow =
        Percentile(&alongForward, parameters.extentLowPercentile);
    const float forwardHigh =
        Percentile(&alongForward, parameters.extentHighPercentile);

    const float halfRight = std::max(0.01F, (rightHigh - rightLow) * 0.5F);
    const float halfUp = std::max(0.01F, (upHigh - upLow) * 0.5F);
    // Depth from one viewpoint only sees the near surface, so the measured
    // forward spread is a lower bound on the object's true depth. It is
    // reported as measured rather than padded to look like a full cuboid.
    const float halfForward = std::max(0.01F, (forwardHigh - forwardLow) * 0.5F);

    // With a prior, keep the measured near face where depth put it and grow
    // the box away from the viewer. Centring the prior on the visible surface
    // instead would push half the cuboid through the object's front face.
    float halfDepth = halfForward;
    float forwardCentre = (forwardLow + forwardHigh) * 0.5F;
    bool depthAssumed = false;
    if (sizePrior.valid && sizePrior.depthMeters > 0.0F) {
        halfDepth = sizePrior.depthMeters * 0.5F;
        // forward points away from the viewer, so the near face is the
        // smaller coordinate along it.
        forwardCentre = forwardLow + halfDepth;
        depthAssumed = true;
    }

    const math::Vec3 centre = math::Add(
        math::Add(
            math::Scale(right, (rightLow + rightHigh) * 0.5F),
            math::Scale(up, (upLow + upHigh) * 0.5F)),
        math::Scale(forward, forwardCentre));

    // Build the orientation quaternion from the basis. right/up/forward are
    // orthonormal by construction above.
    math::Quat orientation{};
    if (!math::RotationFromTo({0.0F, 0.0F, -1.0F}, forward, &orientation)) {
        return fail("Cannot orient the box");
    }
    // Remove any residual roll by re-deriving from the up axis.
    const math::Vec3 rotatedUp = math::Rotate(orientation, {0.0F, 1.0F, 0.0F});
    math::Quat rollCorrection{};
    if (math::RotationFromTo(rotatedUp, up, &rollCorrection)) {
        orientation = math::Multiply(rollCorrection, orientation);
        math::Normalize(&orientation);
    }

    box->centerInLocal = centre;
    box->halfExtents = {halfRight, halfUp, halfDepth};
    box->depthExtentIsAssumed = depthAssumed;
    box->orientationIsMeasured = orientationMeasured;
    box->planeInlierRatio = orientationMeasured
        ? static_cast<float>(cluster.size()) /
              static_cast<float>(std::max<size_t>(1U, inside.size()))
        : 0.0F;
    box->orientationInLocal = orientation;
    box->representativeRangeMeters = clusterCentre;
    box->sampleCount = static_cast<uint32_t>(cluster.size());
    box->inlierRatio = inlierRatio;
    box->confidence = std::clamp(
        inlierRatio * std::min(
            1.0F,
            static_cast<float>(cluster.size()) /
                static_cast<float>(parameters.minimumSampleCount * 4U)),
        0.0F,
        1.0F);
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

}  // namespace questlab::fusion
