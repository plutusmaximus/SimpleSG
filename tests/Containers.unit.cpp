#include <gtest/gtest.h>

#include "Containers.h"

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

// NOLINTBEGIN(readability-magic-numbers)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTBEGIN(bugprone-use-after-move)

namespace
{
struct BoundedVectorLifetimeCounts
{
    int Constructed{ 0 };
    int Destroyed{ 0 };
};

class BoundedVectorTrackedItem
{
public:
    BoundedVectorTrackedItem(BoundedVectorLifetimeCounts& counts, int value)
        : Value(value),
          m_Counts(&counts)
    {
        ++m_Counts->Constructed;
    }

    ~BoundedVectorTrackedItem() { ++m_Counts->Destroyed; }
    BoundedVectorTrackedItem(const BoundedVectorTrackedItem&) = delete;
    BoundedVectorTrackedItem& operator=(const BoundedVectorTrackedItem&) = delete;
    BoundedVectorTrackedItem(BoundedVectorTrackedItem&&) = delete;
    BoundedVectorTrackedItem& operator=(BoundedVectorTrackedItem&&) = delete;

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    int Value;

private:
    BoundedVectorLifetimeCounts* m_Counts;
};

// Suppress SDL assertion dialogs in death-test children. MLG_ABORTIF still aborts.
void IgnoreBoundedVectorAssertionDialogs()
{
    SDL_SetAssertionHandler(
        [](const SDL_AssertData*, void*) -> SDL_AssertState { return SDL_ASSERTION_IGNORE; },
        nullptr);
}

static_assert(!std::is_default_constructible_v<BoundedVector<int>>);
static_assert(!std::is_copy_constructible_v<BoundedVector<int>>);
static_assert(!std::is_copy_assignable_v<BoundedVector<int>>);
static_assert(std::is_nothrow_move_constructible_v<BoundedVector<BoundedVectorTrackedItem>>);
static_assert(std::is_nothrow_move_assignable_v<BoundedVector<BoundedVectorTrackedItem>>);
static_assert(std::is_same_v<decltype(std::declval<const BoundedVector<int>&>()[0]), const int&>);
static_assert(std::is_same_v<decltype(std::declval<const BoundedVector<int>&>().data()), const int*>);
} // namespace

TEST(BoundedVector, StartsEmptyWithoutConstructingElements)
{
    const BoundedVectorLifetimeCounts counts;
    {
        BoundedVector<BoundedVectorTrackedItem> values(3);
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.size(), 0u);
        EXPECT_EQ(values.capacity(), 3u);
        EXPECT_EQ(values.begin(), values.end());
        EXPECT_EQ(values.rbegin(), values.rend());
        EXPECT_EQ(counts.Constructed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 0);
}

TEST(BoundedVector, ZeroCapacityIsEmpty)
{
    const BoundedVector<int> values(0);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(values.size(), 0u);
    EXPECT_EQ(values.capacity(), 0u);
    EXPECT_EQ(values.begin(), values.end());
    EXPECT_EQ(values.rbegin(), values.rend());
}

TEST(BoundedVector, PushBackCopiesLvalue)
{
    BoundedVector<std::vector<int>> values(1);
    std::vector<int> source{ 1, 2 };
    values.push_back(source);
    source[0] = 9;
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values.front(), (std::vector<int>{ 1, 2 }));
    EXPECT_EQ(source[0], 9);
}

TEST(BoundedVector, PushBackMovesRvalue)
{
    BoundedVector<std::unique_ptr<int>> values(1);
    auto source = std::make_unique<int>(42);
    const int* address = source.get();
    values.push_back(std::move(source));
    EXPECT_EQ(source, nullptr);
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values.front().get(), address);
    EXPECT_EQ(*values.front(), 42);
}

TEST(BoundedVector, EmplaceSupportsImmovableElementsAndStableAddresses)
{
    BoundedVectorLifetimeCounts counts;
    BoundedVector<BoundedVectorTrackedItem> values(3);
    auto& first = values.emplace_back(counts, 10);
    auto* address = &first;
    values.emplace_back(counts, 20);
    values.emplace_back(counts, 30);
    EXPECT_EQ(values.size(), 3u);
    EXPECT_EQ(values.capacity(), 3u);
    EXPECT_FALSE(values.empty());
    EXPECT_EQ(values.data(), address);
    EXPECT_EQ(values.data(), address);
    EXPECT_EQ(&values[1], address + 1);
    EXPECT_EQ(&values[2], address + 2);
    EXPECT_EQ(first.Value, 10);
    EXPECT_EQ(counts.Constructed, 3);
    EXPECT_EQ(counts.Destroyed, 0);
}

TEST(BoundedVector, AccessorsAllowMutationAndConstReading)
{
    BoundedVector<int> values(3);
    values.emplace_back(1);
    values.emplace_back(2);
    values.emplace_back(3);
    values.front() = 10;
    values[1] = 20;
    values.back() = 30;
    const auto& constValues = values;
    EXPECT_EQ(constValues.front(), 10);
    EXPECT_EQ(constValues[1], 20);
    EXPECT_EQ(constValues.back(), 30);
    EXPECT_EQ(constValues.data(), &constValues.front());
}

TEST(BoundedVector, IteratesForwardAndBackward)
{
    BoundedVector<int> values(4);
    values.emplace_back(1);
    values.emplace_back(2);
    values.emplace_back(3);
    for(int& value : values)
    {
        value *= 10;
    }
    EXPECT_EQ((std::vector<int>(values.begin(), values.end())), (std::vector<int>{ 10, 20, 30 }));
    EXPECT_EQ((std::vector<int>(values.rbegin(), values.rend())), (std::vector<int>{ 30, 20, 10 }));
    const auto& constValues = values;
    EXPECT_EQ((std::vector<int>(constValues.begin(), constValues.end())),
        (std::vector<int>{ 10, 20, 30 }));
    EXPECT_EQ((std::vector<int>(constValues.rbegin(), constValues.rend())),
        (std::vector<int>{ 30, 20, 10 }));
}

TEST(BoundedVector, SupportsOverAlignedElements)
{
    struct alignas(128) Item
    {
        int Value;
    };
    BoundedVector<Item> values(2);
    values.emplace_back(Item{ 10 });
    values.emplace_back(Item{ 20 });
    for(const auto& value : values)
    {
        const void* p = static_cast<const void*>(&value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(Item), 0u);
    }
    EXPECT_EQ(values.back().Value, 20);
}

TEST(BoundedVector, DestroysOnlyConstructedElements)
{
    BoundedVectorLifetimeCounts counts;
    {
        BoundedVector<BoundedVectorTrackedItem> values(5);
        values.emplace_back(counts, 1);
        values.emplace_back(counts, 2);
        EXPECT_EQ(counts.Constructed, 2);
        EXPECT_EQ(counts.Destroyed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 2);
}

TEST(BoundedVector, MoveConstructionTransfersStorageWithoutMovingElements)
{
    BoundedVectorLifetimeCounts counts;
    {
        BoundedVector<BoundedVectorTrackedItem> source(3);
        auto* address = &source.emplace_back(counts, 42);
        {
            BoundedVector<BoundedVectorTrackedItem> destination(std::move(source));
            EXPECT_EQ(destination.data(), address);
            EXPECT_EQ(destination.size(), 1u);
            EXPECT_EQ(destination.capacity(), 3u);
            EXPECT_EQ(destination.front().Value, 42);
            EXPECT_TRUE(source.empty());
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
            EXPECT_EQ(source.capacity(), 0u);
            EXPECT_EQ(source.begin(), source.end());
            EXPECT_EQ(counts.Constructed, 1);
            EXPECT_EQ(counts.Destroyed, 0);
        }
        EXPECT_EQ(counts.Destroyed, 1);
    }
    EXPECT_EQ(counts.Destroyed, 1);
}

TEST(BoundedVector, MoveAssignmentDestroysOldElementsAndTransfersStorage)
{
    BoundedVectorLifetimeCounts oldCounts;
    BoundedVectorLifetimeCounts newCounts;
    {
        BoundedVector<BoundedVectorTrackedItem> destination(3);
        destination.emplace_back(oldCounts, 1);
        destination.emplace_back(oldCounts, 2);
        {
            BoundedVector<BoundedVectorTrackedItem> source(4);
            auto* address = &source.emplace_back(newCounts, 42);
            auto& result = (destination = std::move(source));
            EXPECT_EQ(&result, &destination);
            EXPECT_EQ(oldCounts.Destroyed, 2);
            EXPECT_EQ(destination.data(), address);
            EXPECT_EQ(destination.size(), 1u);
            EXPECT_EQ(destination.capacity(), 4u);
            EXPECT_TRUE(source.empty());
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
            EXPECT_EQ(source.capacity(), 0u);
        }
        EXPECT_EQ(newCounts.Destroyed, 0);
        EXPECT_EQ(destination.front().Value, 42);
    }
    EXPECT_EQ(oldCounts.Destroyed, 2);
    EXPECT_EQ(newCounts.Constructed, 1);
    EXPECT_EQ(newCounts.Destroyed, 1);
}

TEST(BoundedVector, SelfMovePreservesElements)
{
    BoundedVectorLifetimeCounts counts;
    {
        BoundedVector<BoundedVectorTrackedItem> values(2);
        auto* address = &values.emplace_back(counts, 42);
        auto& alias = values;
        values = std::move(alias);
        EXPECT_EQ(values.data(), address);
        EXPECT_EQ(values.size(), 1u);
        EXPECT_EQ(values.capacity(), 2u);
        EXPECT_EQ(values.front().Value, 42);
        EXPECT_EQ(counts.Destroyed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 1);
}

TEST(BoundedVector, EmptyAndMovedFromContainersCanBeMovedAndReassigned)
{
    BoundedVector<int> source(2);
    BoundedVector<int> destination(std::move(source));
    EXPECT_TRUE(destination.empty());
    EXPECT_EQ(destination.capacity(), 2u);
    destination.emplace_back(42);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    BoundedVector<int> empty(std::move(source));
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.capacity(), 0u);
    source = std::move(destination);
    EXPECT_EQ(source.front(), 42);
    source = std::move(empty);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.capacity(), 0u);
}

TEST(BoundedVector, RejectsInsertionBeyondCapacity)
{
    BoundedVector<int> values(1);
    values.emplace_back(1);
    const int value = 2;
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.push_back(value);
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.push_back(2);
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.emplace_back(2);
        },
        "");
}

TEST(BoundedVector, RejectsInsertionIntoZeroCapacity)
{
    BoundedVector<int> values(0);
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.emplace_back(1);
        },
        "");
}

TEST(BoundedVector, RejectsIndexOutsideConstructedElements)
{
    BoundedVector<int> values(3);
    values.emplace_back(1);
    const auto& constValues = values;
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)values[1];
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)constValues[1];
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)values[3];
        },
        "");
}

TEST(BoundedVector, RejectsAccessToEmptyContainer)
{
    BoundedVector<int> values(1);
    const auto& constValues = values;
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)values.front();
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)values.back();
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)values[0];
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)constValues.front();
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)constValues.back();
        },
        "");
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            (void)constValues[0];
        },
        "");
}

TEST(BoundedVector, RejectsCapacityAboveMaxSize)
{
    using LargeItem = std::array<std::byte, 128>;
    const BoundedVector<LargeItem> values(0);
    const auto tooLarge = values.max_size() + 1;
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            const BoundedVector<LargeItem> oversized(tooLarge);
        },
        "");
}

// NOLINTEND(bugprone-use-after-move)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(readability-magic-numbers)
