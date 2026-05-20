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
    EXPECT_FLOAT_EQ(q.ToVector().x, 1.0f);
    EXPECT_FLOAT_EQ(q.ToVector().y, 2.0f);
    EXPECT_FLOAT_EQ(q.ToVector().z, 3.0f);
    EXPECT_FLOAT_EQ(q.ToVector().w, 4.0f);
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

TEST(Quatf, Normalize)
{
    Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf n = q.Normalize();
    const float len = std::sqrt(n.ToVector().x * n.ToVector().x + n.ToVector().y * n.ToVector().y + n.ToVector().z * n.ToVector().z + n.ToVector().w * n.ToVector().w);
    EXPECT_NEAR(len, 1.0f, EPS);
}

TEST(Quatf, Conjugate)
{
    Quatf q(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = q.Conjugate();
    EXPECT_FLOAT_EQ(c.ToVector().x, -1.0f);
    EXPECT_FLOAT_EQ(c.ToVector().y, 2.0f);
    EXPECT_FLOAT_EQ(c.ToVector().z, -3.0f);
    EXPECT_FLOAT_EQ(c.ToVector().w, -4.0f);
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
    Quatf a(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf b(5.0f, 6.0f, 7.0f, 8.0f);
    Quatf c = a + b;
    EXPECT_FLOAT_EQ(c.ToVector().x, 6.0f);
    EXPECT_FLOAT_EQ(c.ToVector().y, 8.0f);
    EXPECT_FLOAT_EQ(c.ToVector().z, 10.0f);
    EXPECT_FLOAT_EQ(c.ToVector().w, 12.0f);
}

TEST(Quatf, Multiply_Scalar)
{
    Quatf a(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = a * 2.5f;
    EXPECT_FLOAT_EQ(c.ToVector().x, 2.5f);
    EXPECT_FLOAT_EQ(c.ToVector().y, -5.0f);
    EXPECT_FLOAT_EQ(c.ToVector().z, 7.5f);
    EXPECT_FLOAT_EQ(c.ToVector().w, -10.0f);
}

TEST(Quatf, UnaryNegation)
{
    Quatf a(1.0f, -2.0f, 3.0f, -4.0f);
    Quatf c = -a;
    EXPECT_FLOAT_EQ(c.ToVector().x, -1.0f);
    EXPECT_FLOAT_EQ(c.ToVector().y, 2.0f);
    EXPECT_FLOAT_EQ(c.ToVector().z, -3.0f);
    EXPECT_FLOAT_EQ(c.ToVector().w, 4.0f);
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

TEST(Quatf, CompoundMultiplication_Scalar)
{
    Quatf a(1.0f, -2.0f, 3.0f, -4.0f);
    a *= 2.0f;
    EXPECT_FLOAT_EQ(a.ToVector().x, 2.0f);
    EXPECT_FLOAT_EQ(a.ToVector().y, -4.0f);
    EXPECT_FLOAT_EQ(a.ToVector().z, 6.0f);
    EXPECT_FLOAT_EQ(a.ToVector().w, -8.0f);
}

TEST(Quatf, CompoundAddition)
{
    Quatf a(1.0f, 2.0f, 3.0f, 4.0f);
    Quatf b(5.0f, 6.0f, 7.0f, 8.0f);
    a += b;
    EXPECT_FLOAT_EQ(a.ToVector().x, 6.0f);
    EXPECT_FLOAT_EQ(a.ToVector().y, 8.0f);
    EXPECT_FLOAT_EQ(a.ToVector().z, 10.0f);
    EXPECT_FLOAT_EQ(a.ToVector().w, 12.0f);
}
