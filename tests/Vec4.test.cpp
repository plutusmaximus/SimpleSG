#include <gtest/gtest.h>

#include <cmath>

#include "VecMath.h"

// Type alias for convenience
using Vec4f = Vec4<float>;

TEST(Vec4f, Construction_SingleValue)
{
    Vec4f v(2.5f);
    EXPECT_FLOAT_EQ(v.x, 2.5f);
    EXPECT_FLOAT_EQ(v.y, 2.5f);
    EXPECT_FLOAT_EQ(v.z, 2.5f);
    EXPECT_FLOAT_EQ(v.w, 2.5f);
}

TEST(Vec4f, Construction_FourValues)
{
    Vec4f v(1.0f, -3.0f, 4.0f, 2.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, -3.0f);
    EXPECT_FLOAT_EQ(v.z, 4.0f);
    EXPECT_FLOAT_EQ(v.w, 2.0f);
}

TEST(Vec4f, Length)
{
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(v.Length(), std::sqrt(30.0f), 1e-6f);
}

TEST(Vec4f, Normalize)
{
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f n = v.Normalize();
    const float len = std::sqrt(30.0f);
    EXPECT_NEAR(n.x, 1.0f / len, 1e-6f);
    EXPECT_NEAR(n.y, 2.0f / len, 1e-6f);
    EXPECT_NEAR(n.z, 3.0f / len, 1e-6f);
    EXPECT_NEAR(n.w, 4.0f / len, 1e-6f);
    EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
}

TEST(Vec4f, Dot)
{
    Vec4f a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f b(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 70.0f);
}

TEST(Vec4f, Equality)
{
    Vec4f a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f b(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f c(4.0f, 3.0f, 2.0f, 1.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Vec4f, Addition)
{
    Vec4f a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f b(4.0f, 5.0f, 6.0f, 7.0f);
    Vec4f c = a + b;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.0f);
    EXPECT_FLOAT_EQ(c.z, 9.0f);
    EXPECT_FLOAT_EQ(c.w, 11.0f);
}

TEST(Vec4f, Subtraction)
{
    Vec4f a(5.0f, 7.0f, 9.0f, 11.0f);
    Vec4f b(2.0f, 3.0f, 4.0f, 5.0f);
    Vec4f c = a - b;
    EXPECT_FLOAT_EQ(c.x, 3.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
    EXPECT_FLOAT_EQ(c.z, 5.0f);
    EXPECT_FLOAT_EQ(c.w, 6.0f);
}

TEST(Vec4f, Multiply_ComponentWise)
{
    Vec4f a(2.0f, 3.0f, 4.0f, 5.0f);
    Vec4f b(4.0f, 5.0f, 6.0f, 7.0f);
    Vec4f c = a * b;
    EXPECT_FLOAT_EQ(c.x, 8.0f);
    EXPECT_FLOAT_EQ(c.y, 15.0f);
    EXPECT_FLOAT_EQ(c.z, 24.0f);
    EXPECT_FLOAT_EQ(c.w, 35.0f);
}

TEST(Vec4f, Multiply_ScalarRight)
{
    Vec4f a(2.0f, 3.0f, 4.0f, 5.0f);
    Vec4f c = a * 2.5f;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
    EXPECT_FLOAT_EQ(c.z, 10.0f);
    EXPECT_FLOAT_EQ(c.w, 12.5f);
}

TEST(Vec4f, Multiply_ScalarLeft)
{
    Vec4f a(2.0f, 3.0f, 4.0f, 5.0f);
    Vec4f c = 2.5f * a;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
    EXPECT_FLOAT_EQ(c.z, 10.0f);
    EXPECT_FLOAT_EQ(c.w, 12.5f);
}

TEST(Vec4f, UnaryNegation)
{
    Vec4f a(2.0f, -3.0f, 4.0f, -5.0f);
    Vec4f c = -a;
    EXPECT_FLOAT_EQ(c.x, -2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, -4.0f);
    EXPECT_FLOAT_EQ(c.w, 5.0f);
}

TEST(Vec4f, IndexOperator_ReadWrite)
{
    Vec4f v(1.0f, 2.0f, 3.0f, 4.0f);
    v[0] = 3.0f;
    v[1] = 4.0f;
    v[2] = 5.0f;
    v[3] = 6.0f;
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);
    EXPECT_FLOAT_EQ(v[2], 5.0f);
    EXPECT_FLOAT_EQ(v[3], 6.0f);

    const Vec4f cv(7.0f, 8.0f, 9.0f, 10.0f);
    EXPECT_FLOAT_EQ(cv[0], 7.0f);
    EXPECT_FLOAT_EQ(cv[1], 8.0f);
    EXPECT_FLOAT_EQ(cv[2], 9.0f);
    EXPECT_FLOAT_EQ(cv[3], 10.0f);
}

TEST(Vec4f, CompoundAddition)
{
    Vec4f a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f b(4.0f, 5.0f, 6.0f, 7.0f);
    a += b;
    EXPECT_FLOAT_EQ(a.x, 5.0f);
    EXPECT_FLOAT_EQ(a.y, 7.0f);
    EXPECT_FLOAT_EQ(a.z, 9.0f);
    EXPECT_FLOAT_EQ(a.w, 11.0f);
}

TEST(Vec4f, CompoundSubtraction)
{
    Vec4f a(5.0f, 7.0f, 9.0f, 11.0f);
    Vec4f b(2.0f, 3.0f, 4.0f, 5.0f);
    a -= b;
    EXPECT_FLOAT_EQ(a.x, 3.0f);
    EXPECT_FLOAT_EQ(a.y, 4.0f);
    EXPECT_FLOAT_EQ(a.z, 5.0f);
    EXPECT_FLOAT_EQ(a.w, 6.0f);
}

TEST(Vec4f, CompoundMultiplication_Vector)
{
    Vec4f a(2.0f, 3.0f, 4.0f, 5.0f);
    Vec4f b(4.0f, 5.0f, 6.0f, 7.0f);
    a *= b;
    EXPECT_FLOAT_EQ(a.x, 8.0f);
    EXPECT_FLOAT_EQ(a.y, 15.0f);
    EXPECT_FLOAT_EQ(a.z, 24.0f);
    EXPECT_FLOAT_EQ(a.w, 35.0f);
}

TEST(Vec4f, CompoundMultiplication_Scalar)
{
    Vec4f a(2.0f, 3.0f, 4.0f, 5.0f);
    a *= 2.5f;
    EXPECT_FLOAT_EQ(a.x, 5.0f);
    EXPECT_FLOAT_EQ(a.y, 7.5f);
    EXPECT_FLOAT_EQ(a.z, 10.0f);
    EXPECT_FLOAT_EQ(a.w, 12.5f);
}
