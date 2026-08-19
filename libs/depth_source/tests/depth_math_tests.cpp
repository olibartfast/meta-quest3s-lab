#include "depth_source/depth_math.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

questlab::depth::DepthProjection SymmetricProjection(float nearZ, float farZ) {
    questlab::depth::DepthProjection projection;
    projection.nearZ = nearZ;
    projection.farZ = farZ;
    projection.angleLeft = -0.7853982F;   // -45 degrees
    projection.angleRight = 0.7853982F;   // +45 degrees
    projection.angleUp = 0.7853982F;
    projection.angleDown = -0.7853982F;
    return projection;
}

}  // namespace

int main() {
    bool passed = true;
    const float infinity = std::numeric_limits<float>::infinity();

    // OpenGL convention: a stored 0 is the near plane, 1 is the far plane.
    {
        const auto projection = SymmetricProjection(0.1F, 10.0F);
        float depth = 0.0F;
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.0F, projection, &depth) &&
                std::fabs(depth - 0.1F) < 1.0e-4F,
            "stored depth 0 must linearize to nearZ");
        // Halfway is the harmonic mean, which catches a linear-in-z mistake.
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.5F, projection, &depth) &&
                std::fabs(depth - (2.0F * 0.1F * 10.0F) / 10.1F) < 1.0e-4F,
            "mid depth must follow the projective, not linear, mapping");
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.99F, projection, &depth) &&
                depth > 5.0F && depth < 10.0F,
            "depth near 1 must approach farZ from below");
    }

    // Infinite far plane is Meta's normal report, not an error.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        float depth = 0.0F;
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.0F, projection, &depth) &&
                std::fabs(depth - 0.1F) < 1.0e-5F,
            "infinite farZ must still linearize the near plane to nearZ");
        // 1 - nearZ/viewDepth at 2 m with nearZ 0.1 is 0.95.
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.95F, projection, &depth) &&
                std::fabs(depth - 2.0F) < 1.0e-3F,
            "infinite farZ must linearize to nearZ / (1 - stored)");
        // The infinite branch must agree with a large finite far plane.
        auto largeFinite = projection;
        largeFinite.farZ = 1.0e6F;
        float finiteDepth = 0.0F;
        passed &= Expect(
            questlab::depth::LinearizeDepth(0.95F, largeFinite, &finiteDepth) &&
                std::fabs(finiteDepth - depth) < 1.0e-2F,
            "infinite branch must be the limit of the finite one");
    }

    // Invalid input must be rejected rather than producing a plausible number.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        float depth = 0.0F;
        passed &= Expect(
            !questlab::depth::LinearizeDepth(1.0F, projection, &depth),
            "the far limit must be rejected, not returned as infinity");
        passed &= Expect(
            !questlab::depth::LinearizeDepth(1.5F, projection, &depth),
            "depth above 1 must be rejected");
        passed &= Expect(
            !questlab::depth::LinearizeDepth(-0.1F, projection, &depth),
            "negative depth must be rejected");
        passed &= Expect(
            !questlab::depth::LinearizeDepth(
                std::numeric_limits<float>::quiet_NaN(), projection, &depth),
            "NaN depth must be rejected");
        auto broken = projection;
        broken.nearZ = 0.0F;
        passed &= Expect(
            !questlab::depth::LinearizeDepth(0.5F, broken, &depth),
            "a non-positive nearZ must be rejected");
    }

    // The image centre must look straight down view -Z.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        questlab::math::Vec3 direction{};
        passed &= Expect(
            questlab::depth::DepthTexelDirection(
                31.5F, 15.5F, 64, 32, projection, &direction),
            "centre texel direction must resolve");
        passed &= Expect(
            std::fabs(direction.x) < 1.0e-5F &&
                std::fabs(direction.y) < 1.0e-5F &&
                std::fabs(direction.z + 1.0F) < 1.0e-5F,
            "centre texel must look along view -Z");
    }

    // With a 90 degree symmetric FOV the corners sit at 45 degrees, so the
    // lateral offsets equal the depth. This is the check that catches a
    // flipped or half-applied FOV mapping.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        questlab::math::Vec3 point{};
        // Stored 0.95 with nearZ 0.1 gives a planar depth of 2 m.
        passed &= Expect(
            questlab::depth::UnprojectDepthTexel(
                0.0F, 0.0F, 64, 32, 0.95F, projection, &point),
            "top-left texel must unproject");
        passed &= Expect(
            point.z < 0.0F && std::fabs(point.z + 2.0F) < 1.0e-3F,
            "planar depth must land on view -Z at 2 m");
        passed &= Expect(
            point.x < 0.0F && point.y > 0.0F,
            "top-left texel must be left of and above the axis");
        passed &= Expect(
            std::fabs(std::fabs(point.x) - 2.0F) < 0.07F &&
                std::fabs(std::fabs(point.y) - 2.0F) < 0.14F,
            "45 degree corner offsets must match the planar depth");
    }

    // A texel is a planar depth, not a radial range. The corner point must be
    // further from the eye than its linearized depth, and confusing the two is
    // the classic error this guards.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        questlab::math::Vec3 corner{};
        passed &= Expect(
            questlab::depth::UnprojectDepthTexel(
                0.0F, 0.0F, 64, 32, 0.95F, projection, &corner),
            "corner texel must unproject");
        passed &= Expect(
            questlab::math::Length(corner) > 2.0F,
            "corner range must exceed its planar depth");
    }

    // The unreliable near field is rejected, not clamped.
    {
        const auto projection = SymmetricProjection(0.1F, infinity);
        questlab::math::Vec3 point{};
        passed &= Expect(
            !questlab::depth::UnprojectDepthTexel(
                31.5F, 15.5F, 64, 32, 0.0F, projection, &point),
            "a sample inside the near field must be rejected");
    }

    if (passed) {
        std::cout << "depth math tests passed\n";
    }
    return passed ? 0 : 1;
}
