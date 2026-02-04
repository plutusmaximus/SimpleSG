#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

// Type aliases for convenience
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Quatf = Quat<float>;
using Radiansf = Radians<float>;

namespace
{
    constexpr float EPS = 1e-5f;
}

TEST(Quatf, Construction_FromComponents)
{
    Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(q.x, 1.0f);
    EXPECT_FLOAT_EQ(q.y, 2.0f);
    EXPECT_FLOAT_EQ(q.z, 3.0f);
    EXPECT_FLOAT_EQ(q.w, 4.0f);
}

TEST(Quatf, Construction_FromAngleAxis)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);

    const float s = std::sin(angle / 2.0f);
    const float c = std::cos(angle / 2.0f);

    EXPECT_NEAR(q.x, 0.0f, EPS);
    EXPECT_NEAR(q.y, 0.0f, EPS);
    EXPECT_NEAR(q.z, s, EPS);
    EXPECT_NEAR(q.w, c, EPS);
}

TEST(Quatf, Normalize)
{
    Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf n = q.Normalize();
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z + n.w * n.w);
    EXPECT_NEAR(len, 1.0f, EPS);
}

TEST(Quatf, Conjugate)
{
    Quatf q(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = q.Conjugate();
    EXPECT_FLOAT_EQ(c.x, -1.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, -3.0f);
    EXPECT_FLOAT_EQ(c.w, -4.0f);
}

TEST(Quatf, Multiply_Quat)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);
    Quatf identity(0.0f, 0.0f, 0.0f, 1.0f);

    Quatf result = q * identity;
    EXPECT_NEAR(result.x, q.x, EPS);
    EXPECT_NEAR(result.y, q.y, EPS);
    EXPECT_NEAR(result.z, q.z, EPS);
    EXPECT_NEAR(result.w, q.w, EPS);
}

TEST(Quatf, Multiply_VectorRotation)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);

    Vec3f v(1.0f, 0.0f, 0.0f);
    Vec3f r = q * v;

    EXPECT_NEAR(r.x, 0.0f, EPS);
    EXPECT_NEAR(r.y, 1.0f, EPS);
    EXPECT_NEAR(r.z, 0.0f, EPS);
}

TEST(Quatf, GetRotation)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);

    const Vec3f xAxis(1.0f, 0.0f, 0.0f);
    float rotation = q.GetRotation(xAxis);

    EXPECT_NEAR(rotation, std::numbers::pi_v<float>, 1e-4f);
}

TEST(Quatf, Equality)
{
    Quatf a(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf b(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf c(4.0f, 3.0f, 2.0f, 1.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Quatf, Subtract_Quat)
{
    Quatf a(5.0f, 6.0f, 7.0f, 8.0f);
    Quatf b(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf c = a - b;
    EXPECT_FLOAT_EQ(c.x, 4.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
    EXPECT_FLOAT_EQ(c.z, 4.0f);
    EXPECT_FLOAT_EQ(c.w, 4.0f);
}

TEST(Quatf, Subtract_Vec3)
{
    Quatf a(5.0f, 6.0f, 7.0f, 8.0f);
    Vec3f v(1.0f, 2.0f, 3.0f);
    Quatf c = a - v;
    EXPECT_FLOAT_EQ(c.x, 4.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
    EXPECT_FLOAT_EQ(c.z, 4.0f);
    EXPECT_FLOAT_EQ(c.w, 8.0f);
}

TEST(Quatf, Subtract_Vec4)
{
    Quatf a(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf c = a - v;
    EXPECT_FLOAT_EQ(c.x, 4.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
    EXPECT_FLOAT_EQ(c.z, 4.0f);
    EXPECT_FLOAT_EQ(c.w, 4.0f);
}

TEST(Quatf, UnaryNegation)
{
    Quatf a(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = -a;
    EXPECT_FLOAT_EQ(c.x, -1.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
    EXPECT_FLOAT_EQ(c.z, -3.0f);
    EXPECT_FLOAT_EQ(c.w, 4.0f);
}

TEST(Quatf, CompoundMultiplication)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const Vec3f axis(0.0f, 0.0f, 1.0f);
    Quatf q(Radiansf(angle), axis);
    Quatf identity(0.0f, 0.0f, 0.0f, 1.0f);

    q *= identity;
    EXPECT_NEAR(q.x, 0.0f, EPS);
    EXPECT_NEAR(q.y, 0.0f, EPS);
    EXPECT_NEAR(q.z, std::sin(angle / 2.0f), EPS);
    EXPECT_NEAR(q.w, std::cos(angle / 2.0f), EPS);
}
