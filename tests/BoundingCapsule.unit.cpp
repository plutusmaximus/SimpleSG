#include <gtest/gtest.h>

#include "BoundingVolumes.h"

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
void ExpectVec3Eq(const Vec3f& actual, const Vec3f& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}
} // namespace

TEST(BoundingCapsule, Constructor_StoresCenterRadiusAndHalfHeight)
{
    const BoundingCapsule capsule(Vec3f(1.0f, -2.0f, 3.0f), 2.5f, 4.0f);

    ExpectVec3Eq(capsule.GetCenter(), Vec3f(1.0f, -2.0f, 3.0f));
    EXPECT_FLOAT_EQ(capsule.GetRadius(), 2.5f);
    EXPECT_FLOAT_EQ(capsule.GetHalfHeight(), 4.0f);
}

TEST(BoundingCapsule, MultiplyByMat44_TransformsCenterAndKeepsDimensions)
{
    const BoundingCapsule capsule(Vec3f(1.0f, 2.0f, 3.0f), 2.5f, 4.0f);
    // Use TrsTransform to compose 37° rotation about X, Y, Z axes with scale and translation
    const Radiansf angle(37.0f * std::numbers::pi_v<float> / 180.0f);
    const UnitQuatf Qx(angle, Vec3f::XAXIS());
    const UnitQuatf Qy(angle, Vec3f::YAXIS());
    const UnitQuatf Qz(angle, Vec3f::ZAXIS());
    
    TrsTransformf trs;
    trs.R = Qz * Qy * Qx;  // Compose rotations: apply Rx, then Ry, then Rz
    trs.S = Vec3f(1.0f, 1.0f, 1.0f);
    trs.T = Vec3f(4.0f, -5.0f, 6.0f);
    const Mat44f transform = trs.ToMatrix();

    const BoundingCapsule transformed = transform * capsule;

    EXPECT_NEAR(transformed.GetCenter().x, 6.49315f, 1e-4f);
    EXPECT_NEAR(transformed.GetCenter().y, -3.38194f, 1e-4f);
    EXPECT_NEAR(transformed.GetCenter().z, 8.27290f, 1e-4f);
    EXPECT_FLOAT_EQ(transformed.GetRadius(), capsule.GetRadius());
    EXPECT_FLOAT_EQ(transformed.GetHalfHeight(), capsule.GetHalfHeight());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
