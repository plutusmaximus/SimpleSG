#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
// Type alias for convenience
using Radiansf = Radians<float>;

constexpr float PI = std::numbers::pi_v<float>;
constexpr float TWO_PI = 2.0f * PI;
// constexpr float ABS(float value) { return value < 0 ? -value : value; }
constexpr float
EPSILON([[maybe_unused]] float value)
{
    return 1e-6f;
} //{ return ABS(value) * std::numeric_limits<float>::epsilon(); }

// Helper function to normalize a radian value to [0, 2π)
float
NormalizeRadians(const float value)
{
    const float r = value - (std::floor(value / TWO_PI) * TWO_PI);

    if((r >= TWO_PI - EPSILON(TWO_PI)) || (std::abs(r) < EPSILON(TWO_PI)))
    {
        return 0.0f;
    }

    return r;
}
}

// Test construction and initialization
TEST(Radiansf, Construction_DefaultConstructor)
{
    const Radiansf r;
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

TEST(Radiansf, Construction_ExplicitConstructor)
{
    const Radiansf r(PI / 4);
    EXPECT_FLOAT_EQ(r.GetValue(), PI / 4);
}

TEST(Radiansf, Construction_ConstructorWithWrapping)
{
    // Values > 2π should wrap
    const Radiansf r(3 * PI);
    const float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Construction_ConstructorWithNegativeValue)
{
    // Negative values should wrap
    const Radiansf r(-PI / 4);
    const float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Construction_FromDegrees)
{
    const Radiansf r = Radiansf::FromDegrees(90);
    EXPECT_FLOAT_EQ(r.GetValue(), PI / 2);
}

TEST(Radiansf, Construction_FromDegrees360)
{
    const Radiansf r = Radiansf::FromDegrees(360);
    const float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Construction_FromDegreesNegative)
{
    const Radiansf r = Radiansf::FromDegrees(-90);
    const float expected = NormalizeRadians(-PI / 2);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test addition operators
TEST(Radiansf, Addition_AddTwoRadians)
{
    const Radiansf r1(PI / 4);
    const Radiansf r2(PI / 6);
    const Radiansf result = r1 + r2;
    EXPECT_FLOAT_EQ(result.GetValue(), (PI / 4) + (PI / 6));
}

TEST(Radiansf, Addition_AddRadiansAndFloat)
{
    const Radiansf r(PI / 4);
    const Radiansf result = r + (PI / 6);
    EXPECT_FLOAT_EQ(result.GetValue(), (PI / 4) + (PI / 6));
}

TEST(Radiansf, Addition_AdditionWithWrapping)
{
    const Radiansf r1(PI);
    const Radiansf r2(PI);
    const Radiansf result = r1 + r2;
    const float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Addition_AdditionWithLargeWrapping)
{
    const Radiansf r1(7.3f * PI);
    const Radiansf r2(3.7f * PI);
    const Radiansf result = r1 + r2;
    const float expected = NormalizeRadians(11 * PI);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Addition_CompoundAdditionRadians)
{
    Radiansf r1(PI / 4);
    const Radiansf r2(PI / 6);
    r1 += r2;
    EXPECT_FLOAT_EQ(r1.GetValue(), (PI / 4) + (PI / 6));
}

TEST(Radiansf, Addition_CompoundAdditionFloat)
{
    Radiansf r(PI / 4);
    r += PI / 6;
    EXPECT_FLOAT_EQ(r.GetValue(), (PI / 4) + (PI / 6));
}

TEST(Radiansf, Addition_CompoundAdditionWithWrapping)
{
    Radiansf r(PI);
    r += PI;
    const float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test subtraction operators
TEST(Radiansf, Subtraction_SubtractTwoRadians)
{
    const Radiansf r1(PI / 4);
    const Radiansf r2(PI / 6);
    const Radiansf result = r1 - r2;
    EXPECT_FLOAT_EQ(result.GetValue(), (PI / 4) - (PI / 6));
}

TEST(Radiansf, Subtraction_SubtractRadiansAndFloat)
{
    const Radiansf r(PI / 4);
    const Radiansf result = r - (PI / 6);
    EXPECT_FLOAT_EQ(result.GetValue(), (PI / 4) - (PI / 6));
}

TEST(Radiansf, Subtraction_SubtractionWithNegativeWrapping)
{
    const Radiansf r1(PI / 6);
    const Radiansf r2(PI / 4);
    const Radiansf result = r1 - r2;
    const float expected = NormalizeRadians((PI / 6) - (PI / 4));
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Subtraction_UnaryNegation)
{
    const Radiansf r(PI / 4);
    const Radiansf result = -r;
    const float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Subtraction_CompoundSubtractionRadians)
{
    Radiansf r1(PI / 4);
    const Radiansf r2(PI / 6);
    r1 -= r2;
    EXPECT_FLOAT_EQ(r1.GetValue(), (PI / 4) - (PI / 6));
}

TEST(Radiansf, Subtraction_CompoundSubtractionFloat)
{
    Radiansf r(PI / 4);
    r -= PI / 6;
    EXPECT_FLOAT_EQ(r.GetValue(), (PI / 4) - (PI / 6));
}

TEST(Radiansf, Subtraction_CompoundSubtractionWithNegativeWrapping)
{
    Radiansf r(PI / 6);
    r -= PI / 4;
    const float expected = NormalizeRadians((PI / 6) - (PI / 4));
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test multiplication operators
TEST(Radiansf, Multiplication_MultiplyByFloat)
{
    const Radiansf r(PI / 4);
    const Radiansf result = r * 2.0f;
    EXPECT_FLOAT_EQ(result.GetValue(), PI / 2);
}

TEST(Radiansf, Multiplication_MultiplyByFloatLeftAssociative)
{
    const Radiansf r(PI / 4);
    const Radiansf result = 2.0f * r;
    EXPECT_FLOAT_EQ(result.GetValue(), PI / 2);
}

TEST(Radiansf, Multiplication_MultiplyByZero)
{
    const Radiansf r(PI / 4);
    const Radiansf result = r * 0.0f;
    EXPECT_FLOAT_EQ(result.GetValue(), 0.0f);
}

TEST(Radiansf, Multiplication_MultiplyWithWrapping)
{
    const Radiansf r(PI);
    const Radiansf result = r * 2.0f;
    const float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Multiplication_MultiplyByNegative)
{
    const Radiansf r(PI / 4);
    const Radiansf result = r * (-1.0f);
    const float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Multiplication_MultiplyByNegativeLeftAssociative)
{
    const Radiansf r(PI / 4);
    const Radiansf result = (-1.0f) * r;
    const float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(result.GetValue(), expected);
}

TEST(Radiansf, Multiplication_CompoundMultiplication)
{
    Radiansf r(PI / 4);
    r *= 2.0f;
    EXPECT_FLOAT_EQ(r.GetValue(), PI / 2);
}

TEST(Radiansf, Multiplication_CompoundMultiplicationWithWrapping)
{
    Radiansf r(PI);
    r *= 2.0f;
    const float expected = NormalizeRadians(2 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Multiplication_CompoundMultiplicationByNegative)
{
    Radiansf r(PI / 4);
    r *= (-1.0f);
    const float expected = NormalizeRadians(-PI / 4);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test wrapping behavior with complex operations
TEST(Radiansf, Wrapping_WrappingAt2Pi)
{
    const Radiansf r(2 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

TEST(Radiansf, Wrapping_WrappingJustUnderMinus2Pi)
{
    const float value = (-2 * PI) + 0.01f;
    const Radiansf r(value);
    const float expected = value + TWO_PI;
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Wrapping_WrappingJustOver2Pi)
{
    const float value = (2 * PI) + 0.01f;
    const Radiansf r(value);
    const float expected = NormalizeRadians(value);
    EXPECT_NEAR(r.GetValue(), expected, EPSILON(expected));
}

TEST(Radiansf, Wrapping_WrappingMultiple2Pi)
{
    const Radiansf r(6 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

TEST(Radiansf, Wrapping_WrappingNegative)
{
    const Radiansf r(-PI);
    const float expected = NormalizeRadians(-PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Wrapping_WrappingNegativeMultiple)
{
    const Radiansf r(-6 * PI);
    const float expected = NormalizeRadians(-6 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

TEST(Radiansf, Wrapping_AdditionCausingWrapping)
{
    const Radiansf r1(1.8f * PI);
    const Radiansf r2(0.3f * PI);
    const Radiansf result = r1 + r2;
    const float expected = NormalizeRadians(2.1f * PI);
    EXPECT_NEAR(result.GetValue(), expected, EPSILON(expected));
}

TEST(Radiansf, Wrapping_SubtractionCausingWrapping)
{
    const Radiansf r1(0.1f * PI);
    const Radiansf r2(0.2f * PI);
    const Radiansf result = r1 - r2;
    const float expected = NormalizeRadians(-0.1f * PI);
    EXPECT_NEAR(result.GetValue(), expected, EPSILON(expected));
}

TEST(Radiansf, Wrapping_MultiplicationCausingLargeWrapping)
{
    const Radiansf r(PI);
    const Radiansf result = r * 3.5f;
    const float expected = NormalizeRadians(3.5f * PI);
    EXPECT_NEAR(result.GetValue(), expected, EPSILON(expected));
}

// Test assignment operator
TEST(Radiansf, Assignment_AssignmentOperator)
{
    Radiansf r;
    r = PI / 4;
    EXPECT_FLOAT_EQ(r.GetValue(), PI / 4);
}

TEST(Radiansf, Assignment_AssignmentOperatorLargeValueWraps)
{
    Radiansf r;
    r = 3 * PI;
    const float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test comparison operators
TEST(Radiansf, Comparison_EqualityOperator)
{
    const Radiansf r1(PI / 4);
    EXPECT_TRUE(r1 == PI / 4);
}

TEST(Radiansf, Comparison_EqualityOperatorWithEpsilon)
{
    const Radiansf r1(PI / 4);
    // Should be equal within epsilon
    EXPECT_TRUE(r1 == ((PI / 4) + 1e-11f));
}

TEST(Radiansf, Comparison_InequalityOperator)
{
    const Radiansf r1(PI / 4);
    const Radiansf r2(PI / 6);
    EXPECT_TRUE(r1 != r2);
}

TEST(Radiansf, Comparison_InequalityOperatorSameValue)
{
    const Radiansf r1(PI / 4);
    const Radiansf r2(PI / 4);
    EXPECT_FALSE(r1 != r2);
}

// Test GetValue method
TEST(Radiansf, Value_GetValue)
{
    const Radiansf r(PI / 4);
    EXPECT_FLOAT_EQ(r.GetValue(), PI / 4);
}

TEST(Radiansf, Value_GetValueWrapped)
{
    const Radiansf r(3 * PI);
    const float expected = NormalizeRadians(3 * PI);
    EXPECT_FLOAT_EQ(r.GetValue(), expected);
}

// Test complex operator combinations with wrapping
TEST(Radiansf, ComplexOperations_MultipleAdditionsWithWrapping)
{
    Radiansf r = Radiansf::FromDegrees(45);
    r += Radiansf::FromDegrees(90);
    r += Radiansf::FromDegrees(180);
    r += Radiansf::FromDegrees(45);
    // 45 + 90 + 180 + 45 = 360 degrees = 0 radians (after wrapping)
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

TEST(Radiansf, ComplexOperations_MixedOperationsWithWrapping)
{
    Radiansf r = Radiansf::FromDegrees(350);
    r += Radiansf::FromDegrees(20);  // Now should wrap
    r *= 2.0f;  // 370 degrees -> 10 degrees, then 20 degrees
    const float expected = Radiansf::FromDegrees(20).GetValue();
    EXPECT_NEAR(r.GetValue(), expected, EPSILON(expected));
}

TEST(Radiansf, ComplexOperations_AdditionSubtractionCycle)
{
    const float a = 90, b = 100, c = 50, d = 140;
    Radiansf r = Radiansf::FromDegrees(a);
    r += Radiansf::FromDegrees(b);  // 190
    r -= Radiansf::FromDegrees(c);   // 140
    r -= Radiansf::FromDegrees(d);  // 0

    const float error = std::remainder(r.GetValue(), TWO_PI);
    const float epsilon = EPSILON(std::max({a, b, c, d}));
    EXPECT_NEAR(error, 0.0, epsilon);
}

TEST(Radiansf, ComplexOperations_MultiplicationThenAddition)
{
    const Radiansf r(PI / 4);
    const Radiansf result = (r * 2.0f) + (PI / 2);
    EXPECT_FLOAT_EQ(result.GetValue(), PI);
}

TEST(Radiansf, ComplexOperations_AdditionThenMultiplication)
{
    const Radiansf r(PI / 4);
    const Radiansf result = (r + (PI / 4)) * 2.0f;
    EXPECT_FLOAT_EQ(result.GetValue(), PI);
}

TEST(Radiansf, ComplexOperations_LargeValueWrappingChain)
{
    Radiansf r(0.5f * PI);
    const float epsilon = EPSILON(r.GetValue());
    r *= 3.0f;      // 1.5π
    r += 0.8f * PI; // 2.3π (wraps)
    r -= 0.3f * PI; // 2.0π (wraps to 0)
    EXPECT_NEAR(r.GetValue(), 0.0f, epsilon);
}

// Edge cases and boundary conditions
TEST(Radiansf, EdgeCases_VerySmallPositiveValue)
{
    const Radiansf r(1e-7f);
    EXPECT_FLOAT_EQ(r.GetValue(), 0);
}

TEST(Radiansf, EdgeCases_VerySmallNegativeValue)
{
    const Radiansf r(-1e-7f);
    EXPECT_FLOAT_EQ(r.GetValue(), 0);
}

TEST(Radiansf, EdgeCases_ZeroValue)
{
    const Radiansf r(0.0f);
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

TEST(Radiansf, EdgeCases_DefaultValueIsZero)
{
    const Radiansf r;
    EXPECT_FLOAT_EQ(r.GetValue(), 0.0f);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
