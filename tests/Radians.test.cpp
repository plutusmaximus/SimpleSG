#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

// Type alias for convenience
using Radiansf = Radians<float>;

namespace
{
    constexpr float PI = std::numbers::pi_v<float>;
    constexpr float TWO_PI = 2.0f * PI;
    constexpr float ABS(float value) { return value < 0 ? -value : value; }
    constexpr float EPSILON(float value) { return ABS(value) * std::numeric_limits<float>::epsilon(); }

    // Helper function to normalize a radian value to [0, 2π)
    float NormalizeRadians(float value)
    {
        return value - (static_cast<int>(value / TWO_PI) * TWO_PI);
    }
}

// Test construction and initialization
TEST(Radiansf, Construction_DefaultConstructor)
{
    Radiansf r;
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}

TEST(Radiansf, Construction_ExplicitConstructor)
{
    Radiansf r(PI / 4);
    EXPECT_FLOAT_EQ(r.Value(), PI / 4);
}

TEST(Radiansf, Construction_ConstructorWithWrapping)
{
    // Values > 2π should wrap
    Radiansf r(3 * PI);
    float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Construction_ConstructorWithNegativeValue)
{
    // Negative values should wrap
    Radiansf r(-PI / 4);
    float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Construction_FromDegrees)
{
    Radiansf r = Radiansf::FromDegrees(90);
    EXPECT_FLOAT_EQ(r.Value(), PI / 2);
}

TEST(Radiansf, Construction_FromDegrees360)
{
    Radiansf r = Radiansf::FromDegrees(360);
    float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Construction_FromDegreesNegative)
{
    Radiansf r = Radiansf::FromDegrees(-90);
    float expected = NormalizeRadians(-PI / 2);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

// Test addition operators
TEST(Radiansf, Addition_AddTwoRadians)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 6);
    Radiansf result = r1 + r2;
    EXPECT_FLOAT_EQ(result.Value(), PI / 4 + PI / 6);
}

TEST(Radiansf, Addition_AddRadiansAndFloat)
{
    Radiansf r(PI / 4);
    Radiansf result = r + (PI / 6);
    EXPECT_FLOAT_EQ(result.Value(), PI / 4 + PI / 6);
}

TEST(Radiansf, Addition_AdditionWithWrapping)
{
    Radiansf r1(PI);
    Radiansf r2(PI);
    Radiansf result = r1 + r2;
    float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Addition_AdditionWithLargeWrapping)
{
    Radiansf r1(1.5f * PI);
    Radiansf r2(1.5f * PI);
    Radiansf result = r1 + r2;
    float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Addition_CompoundAdditionRadians)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 6);
    r1 += r2;
    EXPECT_FLOAT_EQ(r1.Value(), PI / 4 + PI / 6);
}

TEST(Radiansf, Addition_CompoundAdditionFloat)
{
    Radiansf r(PI / 4);
    r += PI / 6;
    EXPECT_FLOAT_EQ(r.Value(), PI / 4 + PI / 6);
}

TEST(Radiansf, Addition_CompoundAdditionWithWrapping)
{
    Radiansf r(PI);
    r += PI;
    float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

// Test subtraction operators
TEST(Radiansf, Subtraction_SubtractTwoRadians)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 6);
    Radiansf result = r1 - r2;
    EXPECT_FLOAT_EQ(result.Value(), PI / 4 - PI / 6);
}

TEST(Radiansf, Subtraction_SubtractRadiansAndFloat)
{
    Radiansf r(PI / 4);
    Radiansf result = r - (PI / 6);
    EXPECT_FLOAT_EQ(result.Value(), PI / 4 - PI / 6);
}

TEST(Radiansf, Subtraction_SubtractionWithNegativeWrapping)
{
    Radiansf r1(PI / 6);
    Radiansf r2(PI / 4);
    Radiansf result = r1 - r2;
    float expected = NormalizeRadians(PI / 6 - PI / 4);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Subtraction_UnaryNegation)
{
    Radiansf r(PI / 4);
    Radiansf result = -r;
    float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Subtraction_CompoundSubtractionRadians)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 6);
    r1 -= r2;
    EXPECT_FLOAT_EQ(r1.Value(), PI / 4 - PI / 6);
}

TEST(Radiansf, Subtraction_CompoundSubtractionFloat)
{
    Radiansf r(PI / 4);
    r -= PI / 6;
    EXPECT_FLOAT_EQ(r.Value(), PI / 4 - PI / 6);
}

TEST(Radiansf, Subtraction_CompoundSubtractionWithNegativeWrapping)
{
    Radiansf r(PI / 6);
    r -= PI / 4;
    float expected = NormalizeRadians(PI / 6 - PI / 4);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

// Test multiplication operators
TEST(Radiansf, Multiplication_MultiplyByFloat)
{
    Radiansf r(PI / 4);
    Radiansf result = r * 2.0f;
    EXPECT_FLOAT_EQ(result.Value(), PI / 2);
}

TEST(Radiansf, Multiplication_MultiplyByFloatLeftAssociative)
{
    Radiansf r(PI / 4);
    Radiansf result = 2.0f * r;
    EXPECT_FLOAT_EQ(result.Value(), PI / 2);
}

TEST(Radiansf, Multiplication_MultiplyByZero)
{
    Radiansf r(PI / 4);
    Radiansf result = r * 0.0f;
    EXPECT_FLOAT_EQ(result.Value(), 0.0f);
}

TEST(Radiansf, Multiplication_MultiplyWithWrapping)
{
    Radiansf r(PI);
    Radiansf result = r * 2.0f;
    float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Multiplication_MultiplyByNegative)
{
    Radiansf r(PI / 4);
    Radiansf result = r * (-1.0f);
    float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Multiplication_MultiplyByNegativeLeftAssociative)
{
    Radiansf r(PI / 4);
    Radiansf result = (-1.0f) * r;
    float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Multiplication_CompoundMultiplication)
{
    Radiansf r(PI / 4);
    r *= 2.0f;
    EXPECT_FLOAT_EQ(r.Value(), PI / 2);
}

TEST(Radiansf, Multiplication_CompoundMultiplicationWithWrapping)
{
    Radiansf r(PI);
    r *= 2.0f;
    float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Multiplication_CompoundMultiplicationByNegative)
{
    Radiansf r(PI / 4);
    r *= (-1.0f);
    float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

// Test wrapping behavior with complex operations
TEST(Radiansf, Wrapping_WrappingAt2Pi)
{
    Radiansf r(2 * PI);
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}

TEST(Radiansf, Wrapping_WrappingJustUnder2Pi)
{
    float value = 2 * PI - 0.01f;
    Radiansf r(value);
    EXPECT_FLOAT_EQ(r.Value(), value);
}

TEST(Radiansf, Wrapping_WrappingJustOver2Pi)
{
    float value = 2 * PI + 0.01f;
    Radiansf r(value);
    EXPECT_NEAR(r.Value(), 0.01f, EPSILON(value));
}

TEST(Radiansf, Wrapping_WrappingMultiple2Pi)
{
    Radiansf r(6 * PI);
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}

TEST(Radiansf, Wrapping_WrappingNegative)
{
    Radiansf r(-PI);
    float expected = NormalizeRadians(-PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Wrapping_WrappingNegativeMultiple)
{
    Radiansf r(-6 * PI);
    float expected = NormalizeRadians(-6 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, Wrapping_AdditionCausingWrapping)
{
    Radiansf r1(1.8f * PI);
    Radiansf r2(0.3f * PI);
    Radiansf result = r1 + r2;
    float expected = NormalizeRadians(2.1f * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Wrapping_SubtractionCausingWrapping)
{
    Radiansf r1(0.1f * PI);
    Radiansf r2(0.2f * PI);
    Radiansf result = r1 - r2;
    float expected = NormalizeRadians(-0.1f * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

TEST(Radiansf, Wrapping_MultiplicationCausingLargeWrapping)
{
    Radiansf r(PI);
    Radiansf result = r * 3.5f;
    float expected = NormalizeRadians(3.5f * PI);
    EXPECT_FLOAT_EQ(result.Value(), expected);
}

// Test assignment operator
TEST(Radiansf, Assignment_AssignmentOperator)
{
    Radiansf r;
    r = PI / 4;
    EXPECT_FLOAT_EQ(r.Value(), PI / 4);
}

TEST(Radiansf, Assignment_AssignmentOperatorLargeValue)
{
    Radiansf r;
    r = 3 * PI;
    // Note: Assignment operator doesn't wrap, as per implementation
    EXPECT_FLOAT_EQ(r.Value(), 3 * PI);
}

// Test comparison operators
TEST(Radiansf, Comparison_EqualityOperator)
{
    Radiansf r1(PI / 4);
    EXPECT_TRUE(r1 == PI / 4);
}

TEST(Radiansf, Comparison_EqualityOperatorWithEpsilon)
{
    Radiansf r1(PI / 4);
    // Should be equal within epsilon
    EXPECT_TRUE(r1 == (PI / 4 + 1e-11f));
}

TEST(Radiansf, Comparison_InequalityOperator)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 6);
    EXPECT_TRUE(r1 != r2);
}

TEST(Radiansf, Comparison_InequalityOperatorSameValue)
{
    Radiansf r1(PI / 4);
    Radiansf r2(PI / 4);
    EXPECT_FALSE(r1 != r2);
}

// Test GetValue method
TEST(Radiansf, Value_GetValue)
{
    Radiansf r(PI / 4);
    EXPECT_FLOAT_EQ(r.Value(), PI / 4);
}

TEST(Radiansf, Value_GetValueWrapped)
{
    Radiansf r(3 * PI);
    float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

// Test complex operator combinations with wrapping
TEST(Radiansf, ComplexOperations_MultipleAdditionsWithWrapping)
{
    Radiansf r = Radiansf::FromDegrees(45);
    r += Radiansf::FromDegrees(90);
    r += Radiansf::FromDegrees(180);
    r += Radiansf::FromDegrees(45);
    // 45 + 90 + 180 + 45 = 360 degrees = 0 radians (after wrapping)
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}

TEST(Radiansf, ComplexOperations_MixedOperationsWithWrapping)
{
    Radiansf r = Radiansf::FromDegrees(350);
    r += Radiansf::FromDegrees(20);  // Now should wrap
    r *= 2.0f;  // 370 degrees -> 10 degrees, then 20 degrees
    float expected = Radiansf::FromDegrees(20).Value();
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, ComplexOperations_AdditionSubtractionCycle)
{
    const float a = 90, b = 100, c = 50, d = 140;
    Radiansf r = Radiansf::FromDegrees(a);
    r += Radiansf::FromDegrees(b);  // 190
    r -= Radiansf::FromDegrees(c);   // 140
    r -= Radiansf::FromDegrees(d);  // 0
    const float epsilon = EPSILON(std::max({a, b, c, d}));
    EXPECT_NEAR(r.Value(), 0.0f, epsilon);
}

TEST(Radiansf, ComplexOperations_MultiplicationThenAddition)
{
    Radiansf r(PI / 4);
    Radiansf result = (r * 2.0f) + (PI / 2);
    EXPECT_FLOAT_EQ(result.Value(), PI);
}

TEST(Radiansf, ComplexOperations_AdditionThenMultiplication)
{
    Radiansf r(PI / 4);
    Radiansf result = (r + (PI / 4)) * 2.0f;
    EXPECT_FLOAT_EQ(result.Value(), PI);
}

TEST(Radiansf, ComplexOperations_LargeValueWrappingChain)
{
    Radiansf r(0.5f * PI);
    const float epsilon = EPSILON(r.Value());
    r *= 3.0f;      // 1.5π
    r += 0.8f * PI; // 2.3π (wraps)
    r -= 0.3f * PI; // 2.0π (wraps to 0)
    EXPECT_NEAR(r.Value(), 0.0f, epsilon);
}

// Edge cases and boundary conditions
TEST(Radiansf, EdgeCases_VerySmallPositiveValue)
{
    Radiansf r(1e-7f);
    EXPECT_FLOAT_EQ(r.Value(), 1e-7f);
}

TEST(Radiansf, EdgeCases_VerySmallNegativeValue)
{
    Radiansf r(-1e-7f);
    float expected = NormalizeRadians(-1e-7f);
    EXPECT_FLOAT_EQ(r.Value(), expected);
}

TEST(Radiansf, EdgeCases_ZeroValue)
{
    Radiansf r(0.0f);
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}

TEST(Radiansf, EdgeCases_DefaultValueIsZero)
{
    Radiansf r;
    EXPECT_FLOAT_EQ(r.Value(), 0.0f);
}
