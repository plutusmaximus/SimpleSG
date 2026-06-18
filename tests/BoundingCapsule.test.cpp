#include <gtest/gtest.h>

#include "Bounds.h"

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

TEST(BoundingCapsule, TranslateOperator_ReturnsOffsetCapsule)
{
    const BoundingCapsule capsule(Vec3f(-3.0f, 2.0f, 1.0f), 1.5f, 6.0f);
    const Vec3f offset(4.0f, -5.0f, 9.0f);

    const BoundingCapsule translated = capsule + offset;

    ExpectVec3Eq(translated.GetCenter(), Vec3f(1.0f, -3.0f, 10.0f));
    EXPECT_FLOAT_EQ(translated.GetRadius(), capsule.GetRadius());
    EXPECT_FLOAT_EQ(translated.GetHalfHeight(), capsule.GetHalfHeight());
}

TEST(BoundingCapsule, CompoundTranslate_AssignsOffsetCapsule)
{
    BoundingCapsule capsule(Vec3f(0.0f, 0.0f, 0.0f), 3.0f, 7.0f);

    capsule += Vec3f(2.0f, -4.0f, 8.0f);

    ExpectVec3Eq(capsule.GetCenter(), Vec3f(2.0f, -4.0f, 8.0f));
    EXPECT_FLOAT_EQ(capsule.GetRadius(), 3.0f);
    EXPECT_FLOAT_EQ(capsule.GetHalfHeight(), 7.0f);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
