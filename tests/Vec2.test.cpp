#include <gtest/gtest.h>

#include <cmath>

#include "VecMath.h"

// Type alias for convenience
using Vec2f = Vec2<float>;

TEST(Vec2f, Construction_SingleValue)
{
    Vec2f v(2.5f);
    EXPECT_FLOAT_EQ(v.x, 2.5f);
    EXPECT_FLOAT_EQ(v.y, 2.5f);
}

TEST(Vec2f, Construction_TwoValues)
{
    Vec2f v(1.0f, -3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, -3.0f);
}

TEST(Vec2f, Length)
{
    Vec2f v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
}

TEST(Vec2f, Normalize)
{
    Vec2f v(3.0f, 4.0f);
    Vec2f n = v.Normalize();
    EXPECT_NEAR(n.x, 0.6f, 1e-6f);
    EXPECT_NEAR(n.y, 0.8f, 1e-6f);
    EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
}

TEST(Vec2f, Dot)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 11.0f);
}

TEST(Vec2f, Cross)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);
    Vec2f c = a.Cross(b);
    EXPECT_FLOAT_EQ(c.x, -2.0f);
    EXPECT_FLOAT_EQ(c.y, 2.0f);
}

TEST(Vec2f, Equality)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(1.0f, 2.0f);
    Vec2f c(2.0f, 1.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Vec2f, Addition)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);
    Vec2f c = a + b;
    EXPECT_FLOAT_EQ(c.x, 4.0f);
    EXPECT_FLOAT_EQ(c.y, 6.0f);
}

TEST(Vec2f, Subtraction)
{
    Vec2f a(5.0f, 7.0f);
    Vec2f b(2.0f, 3.0f);
    Vec2f c = a - b;
    EXPECT_FLOAT_EQ(c.x, 3.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
}

TEST(Vec2f, Multiply_ComponentWise)
{
    Vec2f a(2.0f, 3.0f);
    Vec2f b(4.0f, 5.0f);
    Vec2f c = a * b;
    EXPECT_FLOAT_EQ(c.x, 8.0f);
    EXPECT_FLOAT_EQ(c.y, 15.0f);
}

TEST(Vec2f, Multiply_ScalarRight)
{
    Vec2f a(2.0f, 3.0f);
    Vec2f c = a * 2.5f;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
}

TEST(Vec2f, Multiply_ScalarLeft)
{
    Vec2f a(2.0f, 3.0f);
    Vec2f c = 2.5f * a;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
}

TEST(Vec2f, UnaryNegation)
{
    Vec2f a(2.0f, -3.0f);
    Vec2f c = -a;
    EXPECT_FLOAT_EQ(c.x, -2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
}

TEST(Vec2f, IndexOperator_ReadWrite)
{
    Vec2f v(1.0f, 2.0f);
    v[0] = 3.0f;
    v[1] = 4.0f;
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);

    const Vec2f cv(5.0f, 6.0f);
    EXPECT_FLOAT_EQ(cv[0], 5.0f);
    EXPECT_FLOAT_EQ(cv[1], 6.0f);
}

TEST(Vec2f, CompoundAddition)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);
    a += b;
    EXPECT_FLOAT_EQ(a.x, 4.0f);
    EXPECT_FLOAT_EQ(a.y, 6.0f);
}

TEST(Vec2f, CompoundSubtraction)
{
    Vec2f a(5.0f, 7.0f);
    Vec2f b(2.0f, 3.0f);
    a -= b;
    EXPECT_FLOAT_EQ(a.x, 3.0f);
    EXPECT_FLOAT_EQ(a.y, 4.0f);
}

TEST(Vec2f, CompoundMultiplication_Vector)
{
    Vec2f a(2.0f, 3.0f);
    Vec2f b(4.0f, 5.0f);
    a *= b;
    EXPECT_FLOAT_EQ(a.x, 8.0f);
    EXPECT_FLOAT_EQ(a.y, 15.0f);
}

TEST(Vec2f, CompoundMultiplication_Scalar)
{
    Vec2f a(2.0f, 3.0f);
    a *= 2.5f;
    EXPECT_FLOAT_EQ(a.x, 5.0f);
    EXPECT_FLOAT_EQ(a.y, 7.5f);
}
