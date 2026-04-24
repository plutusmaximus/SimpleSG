#include <gtest/gtest.h>

#include "SemanticInteger.h"

struct TagA {};
struct TagB {};

using IdA = SemanticInteger<TagA>;
using IdB = SemanticInteger<TagB>;
using SmallId = SemanticInteger<TagA, uint8_t>;
using SignedId = SemanticInteger<TagA, int32_t>;

// --- Construction ---

TEST(SemanticInteger, DefaultConstruction_IsInvalid)
{
    IdA id;
    EXPECT_FALSE(id.IsValid());
}

TEST(SemanticInteger, ExplicitConstruction_IsValid)
{
    IdA id(42u);
    EXPECT_TRUE(id.IsValid());
    EXPECT_EQ(id.Value(), 42u);
}

TEST(SemanticInteger, INVALID_Sentinel_IsInvalid)
{
    EXPECT_FALSE(IdA::INVALID.IsValid());
    EXPECT_EQ(IdA::INVALID, IdA{});
}

TEST(SemanticInteger, CustomInvalidValue)
{
    using ZeroInvalid = SemanticInteger<TagA, uint32_t, 0>;
    ZeroInvalid id;
    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(id.Value(), 0u);

    ZeroInvalid valid(1u);
    EXPECT_TRUE(valid.IsValid());
}

// --- Value ---

TEST(SemanticInteger, Value_ReturnsStoredValue)
{
    IdA id(7u);
    EXPECT_EQ(id.Value(), 7u);
}

// --- Equality ---

TEST(SemanticInteger, Equality_SameValue)
{
    IdA a(10u), b(10u);
    EXPECT_EQ(a, b);
}

TEST(SemanticInteger, Equality_DifferentValues)
{
    IdA a(10u), b(20u);
    EXPECT_NE(a, b);
}

TEST(SemanticInteger, Equality_InvalidEqualsInvalid)
{
    IdA a, b;
    EXPECT_EQ(a, b);
}

TEST(SemanticInteger, Equality_InvalidNotEqualValid)
{
    IdA invalid;
    IdA valid(0u);
    EXPECT_NE(invalid, valid);
}

// --- Ordering ---

TEST(SemanticInteger, LessThan)
{
    IdA a(1u), b(2u);
    EXPECT_LT(a, b);
    EXPECT_FALSE(b < a);
}

TEST(SemanticInteger, GreaterThan)
{
    IdA a(5u), b(3u);
    EXPECT_GT(a, b);
}

TEST(SemanticInteger, LessEqualGreaterEqual)
{
    IdA a(4u), b(4u), c(5u);
    EXPECT_LE(a, b);
    EXPECT_LE(a, c);
    EXPECT_GE(b, a);
    EXPECT_GE(c, a);
}

// --- Tag distinctness (compile-time) ---
// IdA and IdB are different types; this is verified by the type system.
// The following ensures both instantiate independently.
TEST(SemanticInteger, DifferentTags_IndependentInstances)
{
    IdA a(1u);
    IdB b(1u);
    EXPECT_EQ(a.Value(), b.Value()); // same numeric value, different types
}

// --- Wider-type constructor ---

TEST(SemanticInteger, WiderTypeConstructor_uint64_to_uint32)
{
    uint64_t wide = 99;
    IdA id(wide);
    EXPECT_TRUE(id.IsValid());
    EXPECT_EQ(id.Value(), 99u);
}

// --- Small underlying type ---

TEST(SemanticInteger, SmallType_DefaultInvalid)
{
    SmallId id;
    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(id.Value(), std::numeric_limits<uint8_t>::max());
}

TEST(SemanticInteger, SmallType_ValidValue)
{
    SmallId id(static_cast<uint8_t>(0));
    EXPECT_TRUE(id.IsValid());
    EXPECT_EQ(id.Value(), 0u);
}

// --- Signed underlying type ---

TEST(SemanticInteger, SignedType_DefaultInvalid)
{
    SignedId id;
    EXPECT_FALSE(id.IsValid());
}

TEST(SemanticInteger, SignedType_NegativeValue)
{
    SignedId id(-1);
    EXPECT_TRUE(id.IsValid());
    EXPECT_EQ(id.Value(), -1);
}
