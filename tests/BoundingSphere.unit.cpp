#include <gtest/gtest.h>

#include <cmath>

#include "Bounds.h"

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
constexpr float EPS = 1e-6f;

void ExpectVec3Eq(const Vec3f& actual, const Vec3f& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}
} // namespace

TEST(BoundingSphere, Constructor_StoresCenterAndRadius)
{
    const BoundingSphere sphere(Vec3f(1.0f, -2.0f, 3.0f), 2.5f);

    ExpectVec3Eq(sphere.GetCenter(), Vec3f(1.0f, -2.0f, 3.0f));
    EXPECT_FLOAT_EQ(sphere.GetRadius(), 2.5f);
}

TEST(BoundingSphere, Constructor_FromBoundingBox)
{
    const BoundingBox box(Vec3f(-1.0f, -2.0f, -3.0f), Vec3f(3.0f, 2.0f, 1.0f));

    const BoundingSphere sphere(box);

    ExpectVec3Eq(sphere.GetCenter(), Vec3f(1.0f, 0.0f, -1.0f));
    EXPECT_NEAR(sphere.GetRadius(), std::sqrt(12.0f), EPS);
}

TEST(BoundingSphere, Constructor_FromBoundingCapsule)
{
    const BoundingCapsule capsule(Vec3f(-4.0f, 5.0f, 6.0f), 1.5f, 2.0f);

    const BoundingSphere sphere(capsule);

    ExpectVec3Eq(sphere.GetCenter(), Vec3f(-4.0f, 5.0f, 6.0f));
    EXPECT_FLOAT_EQ(sphere.GetRadius(), 3.5f);
}

TEST(BoundingSphere, MergeOperator_WhenContainingSphere_ReturnsContaining)
{
    const BoundingSphere a(Vec3f(0.0f, 0.0f, 0.0f), 10.0f);
    const BoundingSphere b(Vec3f(1.0f, 0.0f, 0.0f), 2.0f);

    const BoundingSphere merged = a + b;

    ExpectVec3Eq(merged.GetCenter(), a.GetCenter());
    EXPECT_FLOAT_EQ(merged.GetRadius(), a.GetRadius());
}

TEST(BoundingSphere, MergeOperator_WhenDisjoint_ComputesEnclosingSphere)
{
    const BoundingSphere a(Vec3f(0.0f, 0.0f, 0.0f), 1.0f);
    const BoundingSphere b(Vec3f(4.0f, 0.0f, 0.0f), 1.0f);

    const BoundingSphere merged = a + b;

    ExpectVec3Eq(merged.GetCenter(), Vec3f(2.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(merged.GetRadius(), 3.0f);
}

TEST(BoundingSphere, CompoundMerge_AssignsMergedSphere)
{
    BoundingSphere a(Vec3f(0.0f, 0.0f, 0.0f), 2.0f);
    const BoundingSphere b(Vec3f(3.0f, 0.0f, 0.0f), 2.0f);

    a += b;

    ExpectVec3Eq(a.GetCenter(), Vec3f(1.5f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(a.GetRadius(), 3.5f);
}

TEST(BoundingSphere, MultiplyByMat44_TransformsCenterAndKeepsRadius)
{
    const BoundingSphere sphere(Vec3f(1.0f, 2.0f, 3.0f), 2.5f);
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

    const BoundingSphere transformed = transform * sphere;

    EXPECT_NEAR(transformed.GetCenter().x, 6.49315f, 1e-4f);
    EXPECT_NEAR(transformed.GetCenter().y, -3.38194f, 1e-4f);
    EXPECT_NEAR(transformed.GetCenter().z, 8.27290f, 1e-4f);
    EXPECT_FLOAT_EQ(transformed.GetRadius(), sphere.GetRadius());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
