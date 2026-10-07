#include "FixedString.h"

#include <format>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>

// NOLINTBEGIN(readability-magic-numbers)

namespace
{
template<size_t N>
void
ExpectFixedString(const FixedString<N>& value, const std::string_view expected)
{
    ASSERT_EQ(value.size(), expected.size());
    EXPECT_EQ(value.empty(), expected.empty());
    EXPECT_EQ(std::string_view(value), expected);
    EXPECT_EQ(std::string_view(value.data(), value.size()), expected);
    EXPECT_EQ(value.data(), value.c_str());
    EXPECT_EQ(std::string_view(value.c_str(), value.size() + 1).back(), '\0');
    EXPECT_EQ(value.GetHashCode(), std::hash<std::string_view>{}(expected));
    EXPECT_EQ(std::hash<FixedString<N>>{}(value), value.GetHashCode());
}
} // namespace

TEST(FixedString, DefaultConstructionIsEmpty)
{
    const FixedString<8> value;
    ExpectFixedString(value, "");
}

TEST(FixedString, DefaultCapacityHolds255Characters)
{
    EXPECT_EQ(FixedString<>::kMaxLength, 255U);
    const std::string expected(255, 'a');
    const FixedString<> value(expected);
    ExpectFixedString(value, expected);
}

TEST(FixedString, ConstructsAtExactCapacity)
{
    const FixedString<5> value("hello");
    ExpectFixedString(value, "hello");
}

TEST(FixedString, AssignsAtExactCapacity)
{
    FixedString<5> value("a");
    auto& result = (value = std::string_view("hello"));
    EXPECT_EQ(&result, &value);
    ExpectFixedString(value, "hello");
}

TEST(FixedString, AssignsShorterAndEmptyStrings)
{
    FixedString<5> value("hello");
    value = "hi";
    ExpectFixedString(value, "hi");
    EXPECT_EQ(value, FixedString<5>("hi"));

    value = "";
    ExpectFixedString(value, "");
    EXPECT_EQ(value, FixedString<5>());
}

TEST(FixedString, CopiesNonTerminatedView)
{
    char source[] = { 'a', 'b', 'c' };
    const FixedString<3> constructed(std::string_view(&source[0], sizeof(source)));
    FixedString<3> assigned;
    assigned = std::string_view(&source[0], sizeof(source));
    source[0] = 'z';

    ExpectFixedString(constructed, "abc");
    ExpectFixedString(assigned, "abc");
}

TEST(FixedString, PreservesEmbeddedNullCharacters)
{
    constexpr char source[] = { 'a', '\0', 'b' };
    const std::string_view expected(&source[0], sizeof(source));
    const FixedString<3> constructed(expected);
    FixedString<3> assigned;
    assigned = expected;

    ExpectFixedString(constructed, expected);
    ExpectFixedString(assigned, expected);
    EXPECT_EQ(constructed, assigned);
    EXPECT_NE(constructed, FixedString<3>("a"));
}

TEST(FixedString, CopyConstructionOwnsItsContents)
{
    FixedString<5> source("hello");
    const FixedString<5> copy(source);
    source = "bye";

    ExpectFixedString(copy, "hello");
    ExpectFixedString(source, "bye");
}

TEST(FixedString, CopyAssignmentReplacesContentsAndHash)
{
    FixedString<5> source("hi");
    FixedString<5> destination("hello");
    destination = source;
    source = "bye";

    ExpectFixedString(destination, "hi");
    ExpectFixedString(source, "bye");
}

TEST(FixedString, MoveConstructionPreservesContentsAndHash)
{
    FixedString<5> source("hello");
    // Exercise the rvalue operation even though FixedString is trivially copyable.
    // NOLINTNEXTLINE(performance-move-const-arg)
    const FixedString<5> destination(std::move(source));
    ExpectFixedString(destination, "hello");
}

TEST(FixedString, MoveAssignmentReplacesContentsAndHash)
{
    FixedString<5> source("hi");
    FixedString<5> destination("hello");
    // NOLINTNEXTLINE(performance-move-const-arg)
    destination = std::move(source);
    ExpectFixedString(destination, "hi");
}

TEST(FixedString, ConstructsFromSmallerCapacity)
{
    FixedString<3> source("abc");
    const FixedString<8> destination(source);
    source = "z";

    ExpectFixedString(destination, "abc");
    ExpectFixedString(source, "z");
}

TEST(FixedString, ComparesContentsLexicographically)
{
    const FixedString<5> first("alpha");
    const FixedString<5> equal("alpha");
    const FixedString<5> later("beta");
    const FixedString<5> prefix("alp");

    EXPECT_EQ(first, equal);
    EXPECT_FALSE(first != equal);
    EXPECT_NE(first, later);
    EXPECT_LT(first, later);
    EXPECT_GT(later, first);
    EXPECT_LT(prefix, first);
    EXPECT_LE(first, equal);
    EXPECT_GE(first, equal);
    EXPECT_EQ(first.GetHashCode(), equal.GetHashCode());
}

TEST(FixedString, FormatsBelowCapacity)
{
    const auto value = FixedString<16>::Format("{}:{}", "item", 7);
    ExpectFixedString(value, "item:7");
}

TEST(FixedString, FormatsEmptyString)
{
    const auto value = FixedString<5>::Format("{}", "");
    ExpectFixedString(value, "");
}

TEST(FixedString, FormatsAtExactCapacityWithoutEllipsis)
{
    const auto value = FixedString<5>::Format("{}", "hello");
    ExpectFixedString(value, "hello");
}

TEST(FixedString, FormattingOverflowAppendsEllipsis)
{
    const auto value = FixedString<5>::Format("{}", "abcdef");
    ExpectFixedString(value, "ab...");
    EXPECT_EQ(value, FixedString<5>("ab..."));
}

TEST(FixedString, ZeroCapacityStoresEmptyString)
{
    FixedString<0> value;
    ExpectFixedString(value, "");
    value = "";
    ExpectFixedString(value, "");
    ExpectFixedString(FixedString<0>(""), "");
    ExpectFixedString(FixedString<0>::Format("{}", "abc"), "");
}

TEST(FixedString, FormattingWithSmallCapacities)
{
    ExpectFixedString(FixedString<1>::Format("{}", "a"), "a");
    ExpectFixedString(FixedString<2>::Format("{}", "ab"), "ab");
    ExpectFixedString(FixedString<3>::Format("{}", "abc"), "abc");

    ExpectFixedString(FixedString<1>::Format("{}", "abcd"), "a");
    ExpectFixedString(FixedString<2>::Format("{}", "abcd"), "ab");
    ExpectFixedString(FixedString<3>::Format("{}", "abcd"), "...");
}

TEST(FixedString, SupportsFmtFormatting)
{
    const FixedString<5> value("hello");
    EXPECT_EQ(fmt::format("[{}]", value), "[hello]");
    EXPECT_EQ(fmt::format("{:>7}", value), "  hello");
}

TEST(FixedString, SupportsStdFormatting)
{
    const FixedString<5> value("hello");
    EXPECT_EQ(std::format("[{}]", value), "[hello]");
    EXPECT_EQ(std::format("{:>7}", value), "  hello");
}

// NOLINTEND(readability-magic-numbers)
