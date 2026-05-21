#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

// Type aliases for convenience
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Quatf = UnitQuat<float>;
using Radiansf = Radians<float>;

namespace
{
    constexpr float EPS = 1e-5f;
}

TEST(Quatf, Construction_FromComponents)
{
    Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
    const float len = std::sqrt(30.0f);
    EXPECT_NEAR(q.ToVector().x, 1.0f / len, EPS);
    EXPECT_NEAR(q.ToVector().y, 2.0f / len, EPS);
    EXPECT_NEAR(q.ToVector().z, 3.0f / len, EPS);
    EXPECT_NEAR(q.ToVector().w, 4.0f / len, EPS);
}

TEST(Quatf, Construction_FromAngleAxis)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);

    const float s = std::sin(angle / 2.0f);
    const float c = std::cos(angle / 2.0f);

    EXPECT_NEAR(q.ToVector().x, 0.0f, EPS);
    EXPECT_NEAR(q.ToVector().y, 0.0f, EPS);
    EXPECT_NEAR(q.ToVector().z, s, EPS);
    EXPECT_NEAR(q.ToVector().w, c, EPS);
}

TEST(Quatf, Conjugate)
{
    Quatf q(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = q.Conjugate();
    const float len = std::sqrt(30.0f);
    EXPECT_NEAR(c.ToVector().x, -1.0f / len, EPS);
    EXPECT_NEAR(c.ToVector().y,  2.0f / len, EPS);
    EXPECT_NEAR(c.ToVector().z, -3.0f / len, EPS);
    EXPECT_NEAR(c.ToVector().w, -4.0f / len, EPS);
}

TEST(Quatf, Multiply_Quat)
{
    const float angle = std::numbers::pi_v<float> / 1.234f;
    const Vec3f axis = Vec3f(1, 2, 3).Normalize();
    Quatf q(Radiansf(angle), axis);
    Quatf identity(0.0f, 0.0f, 0.0f, 1.0f);

    Quatf result = q * identity;
    EXPECT_NEAR(result.ToVector().x, q.ToVector().x, EPS);
    EXPECT_NEAR(result.ToVector().y, q.ToVector().y, EPS);
    EXPECT_NEAR(result.ToVector().z, q.ToVector().z, EPS);
    EXPECT_NEAR(result.ToVector().w, q.ToVector().w, EPS);
}

TEST(Quatf, Multiply_VectorRotation)
{
    const float angle = std::numbers::pi_v<float> / 1.234f;
    const Vec3f axis = Vec3f(1, 2, 3).Normalize();
    Quatf q(Radiansf(angle), axis);

    const Vec3f v(1.0f, 0.0f, 0.0f);
    const Vec3f r = q * v;

    // Rodrigues' rotation formula for rotating v around axis by angle gives:
    const Vec3f rr = v.RotateBy(axis, Radiansf(angle));

    EXPECT_NEAR(r.x, rr.x, EPS);
    EXPECT_NEAR(r.y, rr.y, EPS);
    EXPECT_NEAR(r.z, rr.z, EPS);
}

TEST(Quatf, Equality)
{
    Quatf a(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf b(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf c(4.0f, 3.0f, 2.0f, 1.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Quatf, Add_Quat)
{
    Quatf a(0.0f, 0.0f, 0.0f, 1.0f);
    Quatf b(0.0f, 0.0f, 1.0f, 0.0f);
    Quatf c = a + b;
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(c.ToVector().x, 0.0f,      EPS);
    EXPECT_NEAR(c.ToVector().y, 0.0f,      EPS);
    EXPECT_NEAR(c.ToVector().z, invSqrt2,  EPS);
    EXPECT_NEAR(c.ToVector().w, invSqrt2,  EPS);
}

TEST(Quatf, UnaryNegation)
{
    Quatf a(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = -a;
    EXPECT_NEAR(c.ToVector().x, -a.ToVector().x, EPS);
    EXPECT_NEAR(c.ToVector().y, -a.ToVector().y, EPS);
    EXPECT_NEAR(c.ToVector().z, -a.ToVector().z, EPS);
    EXPECT_NEAR(c.ToVector().w, -a.ToVector().w, EPS);
}

TEST(Quatf, CompoundMultiplication)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);
    Quatf identity(0.0f, 0.0f, 0.0f, 1.0f);

    q *= identity;
    EXPECT_NEAR(q.ToVector().x, 0.0f, EPS);
    EXPECT_NEAR(q.ToVector().y, 0.0f, EPS);
    EXPECT_NEAR(q.ToVector().z, std::sin(angle / 2.0f), EPS);
    EXPECT_NEAR(q.ToVector().w, std::cos(angle / 2.0f), EPS);
}

TEST(Quatf, CompoundAddition)
{
    Quatf a(0.0f, 0.0f, 0.0f, 1.0f);
    Quatf b(0.0f, 0.0f, 1.0f, 0.0f);
    a += b;
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(a.ToVector().x, 0.0f,      EPS);
    EXPECT_NEAR(a.ToVector().y, 0.0f,      EPS);
    EXPECT_NEAR(a.ToVector().z, invSqrt2,  EPS);
    EXPECT_NEAR(a.ToVector().w, invSqrt2,  EPS);
}

TEST(Quatf, Lerp)
{
    const float angleA = std::numbers::pi_v<float> / 2.0f;
    const float angleB = std::numbers::pi_v<float> / 4.0f;
    const Vec3f axisA(0.0f, 0.0f, 1.0f);
    const Vec3f axisB(0.0f, 1.0f, 0.0f);
    Quatf qA(Radiansf(angleA), axisA);
    Quatf qB(Radiansf(angleB), axisB);

    // At t=0, result must equal qA
    const Quatf r0 = qA.Lerp(qB, 0.0f);
    EXPECT_NEAR(r0.ToVector().x, qA.ToVector().x, EPS);
    EXPECT_NEAR(r0.ToVector().y, qA.ToVector().y, EPS);
    EXPECT_NEAR(r0.ToVector().z, qA.ToVector().z, EPS);
    EXPECT_NEAR(r0.ToVector().w, qA.ToVector().w, EPS);

    // At t=1, result must equal qB
    const Quatf r1 = qA.Lerp(qB, 1.0f);
    EXPECT_NEAR(r1.ToVector().x, qB.ToVector().x, EPS);
    EXPECT_NEAR(r1.ToVector().y, qB.ToVector().y, EPS);
    EXPECT_NEAR(r1.ToVector().z, qB.ToVector().z, EPS);
    EXPECT_NEAR(r1.ToVector().w, qB.ToVector().w, EPS);

    // At t=0.5, result must be the normalized midpoint of the two component vectors
    const Quatf r05 = qA.Lerp(qB, 0.5f);
    const Vec4f expected = ((qA.ToVector() + qB.ToVector()) * 0.5f).Normalize();
    EXPECT_NEAR(r05.ToVector().x, expected.x, EPS);
    EXPECT_NEAR(r05.ToVector().y, expected.y, EPS);
    EXPECT_NEAR(r05.ToVector().z, expected.z, EPS);
    EXPECT_NEAR(r05.ToVector().w, expected.w, EPS);
}

TEST(Quatf, InverseRotateVector)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);

    const Vec3f v(1.0f, 0.0f, 0.0f);

    const Mat44f invMat1 = q.Inverse().ToMatrix();
    const Mat44f invMat2 = q.ToMatrix().InverseAffine();

    const Vec4f r1 = invMat1 * v;
    const Vec4f r2 = invMat2 * v;

    EXPECT_NEAR(r1.x, 0.0f, EPS);
    EXPECT_NEAR(r1.y, -1.0f, EPS);
    EXPECT_NEAR(r1.z, 0.0f, EPS);

    EXPECT_NEAR(r2.x, 0.0f, EPS);
    EXPECT_NEAR(r2.y, -1.0f, EPS);
    EXPECT_NEAR(r2.z, 0.0f, EPS);

    const Vec4f diff = r1 - r2;
    EXPECT_NEAR(diff.x, 0.0f, EPS);
    EXPECT_NEAR(diff.y, 0.0f, EPS);
    EXPECT_NEAR(diff.z, 0.0f, EPS);
    EXPECT_NEAR(diff.w, 0.0f, EPS);
}