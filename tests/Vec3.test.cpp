#include <gtest/gtest.h>

#include <cmath>

#include "VecMath.h"

// Type alias for convenience
using Vec3f = Vec3<float>;

TEST(Vec3f, Construction_SingleValue)
{
    Vec3f v(2.5f);
    EXPECT_FLOAT_EQ(v.x, 2.5f);
    EXPECT_FLOAT_EQ(v.y, 2.5f);
    EXPECT_FLOAT_EQ(v.z, 2.5f);
}

TEST(Vec3f, Construction_ThreeValues)
{
    Vec3f v(1.0f, -3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, -3.0f);
    EXPECT_FLOAT_EQ(v.z, 4.0f);
}

TEST(Vec3f, AxisConstants)
{
    Vec3f x = Vec3f::XAXIS();
    Vec3f y = Vec3f::YAXIS();
    Vec3f z = Vec3f::ZAXIS();

    EXPECT_FLOAT_EQ(x.x, 1.0f);
    EXPECT_FLOAT_EQ(x.y, 0.0f);
    EXPECT_FLOAT_EQ(x.z, 0.0f);

    EXPECT_FLOAT_EQ(y.x, 0.0f);
    EXPECT_FLOAT_EQ(y.y, 1.0f);
    EXPECT_FLOAT_EQ(y.z, 0.0f);

    EXPECT_FLOAT_EQ(z.x, 0.0f);
    EXPECT_FLOAT_EQ(z.y, 0.0f);
    EXPECT_FLOAT_EQ(z.z, 1.0f);
}

TEST(Vec3f, Length)
{
    Vec3f v(3.0f, 4.0f, 12.0f);
    EXPECT_FLOAT_EQ(v.Length(), 13.0f);
}

TEST(Vec3f, Normalize)
{
    Vec3f v(3.0f, 4.0f, 12.0f);
    Vec3f n = v.Normalize();
    EXPECT_NEAR(n.x, 3.0f / 13.0f, 1e-6f);
    EXPECT_NEAR(n.y, 4.0f / 13.0f, 1e-6f);
    EXPECT_NEAR(n.z, 12.0f / 13.0f, 1e-6f);
    EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
}

TEST(Vec3f, Dot)
{
    Vec3f a(1.0f, 2.0f, 3.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 32.0f);
}

TEST(Vec3f, Cross)
{
    Vec3f a(1.0f, 0.0f, 0.0f);
    Vec3f b(0.0f, 1.0f, 0.0f);
    Vec3f c = a.Cross(b);
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);
}

TEST(Vec3f, Equality)
{
    Vec3f a(1.0f, 2.0f, 3.0f);
    Vec3f b(1.0f, 2.0f, 3.0f);
    Vec3f c(3.0f, 2.0f, 1.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Vec3f, Addition)
{
    Vec3f a(1.0f, 2.0f, 3.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);
    Vec3f c = a + b;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.0f);
    EXPECT_FLOAT_EQ(c.z, 9.0f);
}

TEST(Vec3f, Subtraction)
{
    Vec3f a(5.0f, 7.0f, 9.0f);
    Vec3f b(2.0f, 3.0f, 4.0f);
    Vec3f c = a - b;
    EXPECT_FLOAT_EQ(c.x, 3.0f);
    EXPECT_FLOAT_EQ(c.y, 4.0f);
    EXPECT_FLOAT_EQ(c.z, 5.0f);
}

TEST(Vec3f, Multiply_ComponentWise)
{
    Vec3f a(2.0f, 3.0f, 4.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);
    Vec3f c = a * b;
    EXPECT_FLOAT_EQ(c.x, 8.0f);
    EXPECT_FLOAT_EQ(c.y, 15.0f);
    EXPECT_FLOAT_EQ(c.z, 24.0f);
}

TEST(Vec3f, Multiply_ScalarRight)
{
    Vec3f a(2.0f, 3.0f, 4.0f);
    Vec3f c = a * 2.5f;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
    EXPECT_FLOAT_EQ(c.z, 10.0f);
}

TEST(Vec3f, Multiply_ScalarLeft)
{
    Vec3f a(2.0f, 3.0f, 4.0f);
    Vec3f c = 2.5f * a;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.5f);
    EXPECT_FLOAT_EQ(c.z, 10.0f);
}

TEST(Vec3f, UnaryNegation)
{
    Vec3f a(2.0f, -3.0f, 4.0f);
    Vec3f c = -a;
    EXPECT_FLOAT_EQ(c.x, -2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, -4.0f);
}

TEST(Vec3f, IndexOperator_ReadWrite)
{
    Vec3f v(1.0f, 2.0f, 3.0f);
    v[0] = 3.0f;
    v[1] = 4.0f;
    v[2] = 5.0f;
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);
    EXPECT_FLOAT_EQ(v[2], 5.0f);

    const Vec3f cv(6.0f, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(cv[0], 6.0f);
    EXPECT_FLOAT_EQ(cv[1], 7.0f);
    EXPECT_FLOAT_EQ(cv[2], 8.0f);
}

TEST(Vec3f, CompoundAddition)
{
    Vec3f a(1.0f, 2.0f, 3.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);
    a += b;
    EXPECT_FLOAT_EQ(a.x, 5.0f);
    EXPECT_FLOAT_EQ(a.y, 7.0f);
    EXPECT_FLOAT_EQ(a.z, 9.0f);
}

TEST(Vec3f, CompoundSubtraction)
{
    Vec3f a(5.0f, 7.0f, 9.0f);
    Vec3f b(2.0f, 3.0f, 4.0f);
    a -= b;
    EXPECT_FLOAT_EQ(a.x, 3.0f);
    EXPECT_FLOAT_EQ(a.y, 4.0f);
    EXPECT_FLOAT_EQ(a.z, 5.0f);
}

TEST(Vec3f, CompoundMultiplication_Vector)
{
    Vec3f a(2.0f, 3.0f, 4.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);
    a *= b;
    EXPECT_FLOAT_EQ(a.x, 8.0f);
    EXPECT_FLOAT_EQ(a.y, 15.0f);
    EXPECT_FLOAT_EQ(a.z, 24.0f);
}

TEST(Vec3f, CompoundMultiplication_Scalar)
{
    Vec3f a(2.0f, 3.0f, 4.0f);
    a *= 2.5f;
    EXPECT_FLOAT_EQ(a.x, 5.0f);
    EXPECT_FLOAT_EQ(a.y, 7.5f);
    EXPECT_FLOAT_EQ(a.z, 10.0f);
}
