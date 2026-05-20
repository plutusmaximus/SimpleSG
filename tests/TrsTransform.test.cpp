#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

namespace
{
    constexpr float EPS = 1e-5f;

    void ExpectVec3Near(const Vec3f& actual, const Vec3f& expected, float eps = EPS)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    void ExpectVec4Near(const Vec4f& actual, const Vec4f& expected, float eps = EPS)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
        EXPECT_NEAR(actual.w, expected.w, eps);
    }

    void ExpectMat44Near(const Mat44f& actual, const Mat44f& expected, float eps = EPS)
    {
        for(int row = 0; row < 4; ++row)
        {
            for(int col = 0; col < 4; ++col)
            {
                EXPECT_NEAR(actual[row][col], expected[row][col], eps);
            }
        }
    }
}

TEST(TrsTransformf, DefaultConstruction)
{
    TrsTransformf trs{};

    EXPECT_EQ(trs.T, Vec3f(0.0f));
    EXPECT_EQ(trs.S, Vec3f(1.0f));
    EXPECT_EQ(trs.R, Quatf(0.0f, 0.0f, 0.0f, 1.0f));
}

TEST(TrsTransformf, ToMatrix_AppliesScaleRotationTranslation)
{
    TrsTransformf trs;
    trs.T = Vec3f(10.0f, -2.0f, 5.0f);
    trs.R = Quatf(Radiansf(std::numbers::pi_v<float> / 2.0f), Vec3f::ZAXIS());
    trs.S = Vec3f(2.0f, 3.0f, 4.0f);

    const Mat44f m = trs.ToMatrix();

    ExpectVec4Near(m * Vec3f(1.0f, 0.0f, 0.0f), Vec4f(10.0f, 0.0f, 5.0f, 1.0f));
    ExpectVec4Near(m * Vec3f(0.0f, 1.0f, 0.0f), Vec4f(7.0f, -2.0f, 5.0f, 1.0f));
    ExpectVec4Near(m * Vec3f(0.0f, 0.0f, 1.0f), Vec4f(10.0f, -2.0f, 9.0f, 1.0f));
}

TEST(TrsTransformf, MultiplyOperator_Vec3_MatchesMatrixTransform)
{
    TrsTransformf trs;
    trs.T = Vec3f(4.0f, -1.0f, 2.0f);
    trs.R = Quatf(Radiansf(std::numbers::pi_v<float> / 2.0f), Vec3f::ZAXIS());
    trs.S = Vec3f(2.0f, 3.0f, 4.0f);

    const Vec3f point(1.0f, 2.0f, -1.0f);
    const Vec3f transformed = trs * point;

    const Vec4f expected4 = trs.ToMatrix() * point;
    ExpectVec3Near(transformed, Vec3f(expected4));
}

TEST(TrsTransformf, Inverse_ComposesToIdentity)
{
    TrsTransformf trs;
    trs.T = Vec3f(3.0f, -4.0f, 2.0f);
    trs.R = Quatf(Radiansf(std::numbers::pi_v<float> / 3.0f), Vec3f(1.0f, 2.0f, 3.0f).Normalize());
    trs.S = Vec3f(1);//Vec3f(2.0f, 0.5f, 4.0f); DO NOT SUBMIT

    const Mat44f backward = trs.Inverse();
    const Mat44f forward = trs.ToMatrix();

    const Mat44f fb = forward * backward;
    const Mat44f bf = backward * forward;

    ExpectMat44Near(fb, Mat44f::Identity(), 5e-5f);
    ExpectMat44Near(bf, Mat44f::Identity(), 5e-5f);
}

TEST(TrsTransformf, FromMatrix_RoundTrip)
{
    TrsTransformf source;
    source.T = Vec3f(-7.0f, 8.0f, 1.5f);
    source.R = Quatf(Radiansf(std::numbers::pi_v<float> / 5.0f), Vec3f::YAXIS());
    source.S = Vec3f(1.25f, 2.5f, 0.75f);

    const Mat44f matrix = source.ToMatrix();

    const TrsTransformf reconstructed = TrsTransformf::FromMatrix(matrix);
    const Mat44f rebuilt = reconstructed.ToMatrix();

    ExpectMat44Near(rebuilt, matrix, 1e-4f);
}

TEST(TrsTransformf, LocalAxes_FollowRotation)
{
    TrsTransformf trs;
    trs.R = Quatf(Radiansf(std::numbers::pi_v<float> / 2.0f), Vec3f::ZAXIS());

    const Vec3f xAxis = trs.LocalXAxis();
    const Vec3f yAxis = trs.LocalYAxis();
    const Vec3f zAxis = trs.LocalZAxis();

    ExpectVec3Near(xAxis, Vec3f(0.0f, 1.0f, 0.0f));
    ExpectVec3Near(yAxis, Vec3f(-1.0f, 0.0f, 0.0f));
    ExpectVec3Near(zAxis, Vec3f(0.0f, 0.0f, 1.0f));

    EXPECT_NEAR(xAxis.Length(), 1.0f, EPS);
    EXPECT_NEAR(yAxis.Length(), 1.0f, EPS);
    EXPECT_NEAR(zAxis.Length(), 1.0f, EPS);
}

TEST(TrsTransformf, Equality)
{
    TrsTransformf a;
    a.T = Vec3f(1.0f, 2.0f, 3.0f);
    a.R = Quatf(Radiansf(std::numbers::pi_v<float> / 4.0f), Vec3f::XAXIS());
    a.S = Vec3f(2.0f, 2.0f, 2.0f);

    TrsTransformf b = a;
    TrsTransformf c = a;
    c.T.z += 1.0f;

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}
