#include <gtest/gtest.h>

#include "Containers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

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
static_assert(std::is_default_constructible_v<InplaceVector<int, 3>>);
static_assert(!std::is_copy_constructible_v<InplaceVector<int, 3>>);
static_assert(!std::is_copy_assignable_v<InplaceVector<int, 3>>);
static_assert(
    std::is_same_v<decltype(std::declval<const InplaceVector<int, 3>&>()[0]), const int&>);
static_assert(
    std::is_same_v<decltype(std::declval<const InplaceVector<int, 3>&>().data()), const int*>);

struct ContainerMoveCounts
{
    int Constructed{ 0 };
    int Moved{ 0 };
    int Assigned{ 0 };
    int Destroyed{ 0 };
};

// Deleted assignment makes assigning into raw storage a compile-time regression.
class ContainerMoveConstructOnly
{
public:
    ContainerMoveConstructOnly(ContainerMoveCounts& counts, int value)
        : Value(std::make_unique<int>(value)),
          m_Counts(&counts)
    {
        ++m_Counts->Constructed;
    }

    ContainerMoveConstructOnly(ContainerMoveConstructOnly&& other) noexcept
        : Value(std::move(other.Value)),
          m_Counts(other.m_Counts)
    {
        ++m_Counts->Constructed;
        ++m_Counts->Moved;
    }

    ~ContainerMoveConstructOnly() { ++m_Counts->Destroyed; }
    ContainerMoveConstructOnly(const ContainerMoveConstructOnly&) = delete;
    ContainerMoveConstructOnly& operator=(const ContainerMoveConstructOnly&) = delete;
    ContainerMoveConstructOnly& operator=(ContainerMoveConstructOnly&&) = delete;

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::unique_ptr<int> Value;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    ContainerMoveCounts* m_Counts;
};

class ContainerMoveAssignable
{
public:
    ContainerMoveAssignable(ContainerMoveCounts& counts, int value)
        : Value(std::make_unique<int>(value)),
          m_Counts(&counts)
    {
        ++m_Counts->Constructed;
    }

    ContainerMoveAssignable(ContainerMoveAssignable&& other) noexcept
        : Value(std::move(other.Value)),
          m_Counts(other.m_Counts)
    {
        ++m_Counts->Constructed;
        ++m_Counts->Moved;
    }

    ContainerMoveAssignable& operator=(ContainerMoveAssignable&& other) noexcept
    {
        if(this != &other)
        {
            // Assignment must observe the destination's existing, live state.
            ValueBeforeAssignment = Value ? *Value : -1;
            Value = std::move(other.Value);
            ++m_Counts->Assigned;
        }
        return *this;
    }

    ~ContainerMoveAssignable() { ++m_Counts->Destroyed; }
    ContainerMoveAssignable(const ContainerMoveAssignable&) = delete;
    ContainerMoveAssignable& operator=(const ContainerMoveAssignable&) = delete;

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::unique_ptr<int> Value;
    int ValueBeforeAssignment{ -1 };
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    ContainerMoveCounts* m_Counts;
};

void
CheckInplaceMoveAssignment(size_t destinationSize, size_t sourceSize)
{
    ContainerMoveCounts counts;
    const auto overlap = std::min(destinationSize, sourceSize);
    const auto extra = sourceSize - overlap;
    const auto excess = destinationSize - overlap;
    const auto constructed = static_cast<int>(destinationSize + sourceSize + extra);
    {
        InplaceVector<ContainerMoveAssignable, 3> destination;
        for(size_t i = 0; i < destinationSize; ++i)
        {
            destination.emplace_back(counts, 10 + static_cast<int>(i));
        }
        const auto* destinationAddress = destination.data();
        {
            InplaceVector<ContainerMoveAssignable, 3> source;
            for(size_t i = 0; i < sourceSize; ++i)
            {
                source.emplace_back(counts, 40 + static_cast<int>(i));
            }
            const auto* sourceAddress = source.data();
            auto& result = (destination = std::move(source));
            EXPECT_EQ(&result, &destination);
            ASSERT_EQ(destination.size(), sourceSize);
            EXPECT_EQ(destination.capacity(), 3u);
            if(overlap > 0)
            {
                EXPECT_EQ(destination.data(), destinationAddress);
            }
            if(sourceSize > 0)
            {
                EXPECT_NE(destination.data(), sourceAddress);
            }
            EXPECT_EQ(counts.Assigned, static_cast<int>(overlap));
            EXPECT_EQ(counts.Moved, static_cast<int>(extra));
            EXPECT_EQ(counts.Constructed, constructed);
            EXPECT_EQ(counts.Destroyed, static_cast<int>(excess));
            for(size_t i = 0; i < sourceSize; ++i)
            {
                ASSERT_NE(destination[i].Value, nullptr);
                EXPECT_EQ(*destination[i].Value, 40 + static_cast<int>(i));
                EXPECT_EQ(destination[i].ValueBeforeAssignment,
                    i < overlap ? 10 + static_cast<int>(i) : -1);
            }
            // Inline assignment leaves the source elements alive but moved from.
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
            ASSERT_EQ(source.size(), sourceSize);
            EXPECT_EQ(source.capacity(), 3u);
            EXPECT_EQ(source.data(), sourceAddress);
            for(const auto& item : source)
            {
                EXPECT_EQ(item.Value, nullptr);
            }
        }
        EXPECT_EQ(counts.Destroyed, static_cast<int>(excess + sourceSize));
        for(size_t i = 0; i < sourceSize; ++i)
        {
            ASSERT_NE(destination[i].Value, nullptr);
            EXPECT_EQ(*destination[i].Value, 40 + static_cast<int>(i));
        }
    }
    EXPECT_EQ(counts.Destroyed, constructed);
}

struct ContainerEraseCounts
{
    int Constructed{ 0 };
    int Assigned{ 0 };
    int Destroyed{ 0 };
};

class ContainerEraseItem
{
public:
    ContainerEraseItem(ContainerEraseCounts& counts, int value)
        : Value(value),
          m_Counts(&counts)
    {
        ++m_Counts->Constructed;
    }

    ~ContainerEraseItem() { ++m_Counts->Destroyed; }
    ContainerEraseItem(const ContainerEraseItem&) = delete;
    ContainerEraseItem& operator=(const ContainerEraseItem&) = delete;
    ContainerEraseItem(ContainerEraseItem&&) = delete;
    // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
    ContainerEraseItem& operator=(ContainerEraseItem&& other) noexcept
    {
        // A self-move changes the value too, exposing accidental self-assignment.
        Value = other.Value;
        other.Value = -1;
        ++m_Counts->Assigned;
        return *this;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    int Value;

private:
    ContainerEraseCounts* m_Counts;
};

template<typename Container>
void
CheckAppendRange(Container& values)
{
    const std::array<int, 0> empty{};
    values.append_range(empty);
    EXPECT_TRUE(values.empty());
    values.emplace_back(10);
    const std::array input{ 20, 30, 40 };
    values.append_range(input);
    values.append_range(empty);
    ASSERT_EQ(values.size(), 4u);
    EXPECT_EQ(values.size(), values.capacity());
    const auto& constValues = values;
    EXPECT_EQ((std::vector<int>(constValues.cbegin(), constValues.cend())),
        (std::vector<int>{ 10, 20, 30, 40 }));
}

template<typename Container>
void
CheckErasePositions(Container& values, ContainerEraseCounts& counts)
{
    for(const int value : { 10, 20, 30, 40, 50 })
    {
        values.emplace_back(counts, value);
    }

    auto next = values.erase(values.cbegin());
    ASSERT_EQ(values.size(), 4u);
    ASSERT_EQ(next, values.begin());
    EXPECT_EQ(next->Value, 20);
    EXPECT_EQ(counts.Destroyed, 1);

    next = values.erase(values.cbegin() + 1);
    ASSERT_EQ(values.size(), 3u);
    ASSERT_EQ(next, values.begin() + 1);
    EXPECT_EQ(next->Value, 40);
    EXPECT_EQ(values.front().Value, 20);
    EXPECT_EQ(values.back().Value, 50);
    EXPECT_EQ(counts.Destroyed, 2);

    next = values.erase(values.cend() - 1);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(next, values.end());
    EXPECT_EQ(values.front().Value, 20);
    EXPECT_EQ(values.back().Value, 40);
    EXPECT_EQ(counts.Destroyed, 3);
    EXPECT_EQ(values.capacity(), 5u);
}

template<typename Container>
void
CheckEraseRangeAndReinsert(Container& values, ContainerEraseCounts& counts)
{
    for(const int value : { 10, 20, 30, 40, 50 })
    {
        values.emplace_back(counts, value);
    }
    const auto* address = values.data();
    auto next = values.erase(values.cbegin() + 1, values.cbegin() + 4);
    ASSERT_EQ(values.size(), 2u);
    ASSERT_EQ(next, values.begin() + 1);
    EXPECT_EQ(values.front().Value, 10);
    EXPECT_EQ(next->Value, 50);
    EXPECT_EQ(counts.Destroyed, 3);

    next = values.erase(values.cbegin(), values.cend());
    EXPECT_EQ(next, values.end());
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(values.capacity(), 5u);
    EXPECT_EQ(counts.Destroyed, 5);
    // Erasing an empty range after clearing must preserve the insertion position.
    EXPECT_EQ(values.erase(values.cbegin(), values.cend()), values.end());
    values.emplace_back(counts, 60);
    EXPECT_EQ(values.data(), address);
    EXPECT_EQ(values.front().Value, 60);
    EXPECT_EQ(values.size(), 1u);
    EXPECT_EQ(counts.Constructed, 6);
}

template<typename Container>
void
CheckEmptyErase(Container& values, ContainerEraseCounts& counts)
{
    EXPECT_EQ(values.erase(values.cbegin(), values.cend()), values.end());
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(counts.Assigned, 0);
    EXPECT_EQ(counts.Destroyed, 0);
    values.emplace_back(counts, 10);
    values.emplace_back(counts, 20);
    const auto* address = values.data();
    // Cover an empty range at the beginning, middle, and end.
    for(std::ptrdiff_t offset = 0; offset <= 2; ++offset)
    {
        EXPECT_EQ(values.erase(values.cbegin() + offset, values.cbegin() + offset),
            values.begin() + offset);
    }
    EXPECT_EQ(values.data(), address);
    EXPECT_EQ(values.size(), 2u);
    EXPECT_EQ(values.front().Value, 10);
    EXPECT_EQ(values.back().Value, 20);
    EXPECT_EQ(counts.Assigned, 0);
    EXPECT_EQ(counts.Destroyed, 0);
}
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
    EXPECT_EQ((std::vector<int>(constValues.cbegin(), constValues.cend())),
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
            EXPECT_EQ(source.capacity(), 3u);
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
            EXPECT_EQ(source.capacity(), 4u);
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
    EXPECT_EQ(empty.capacity(), 2u);
    source = std::move(destination);
    EXPECT_EQ(source.front(), 42);
    source = std::move(empty);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.capacity(), 2u);
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

TEST(InplaceVector, StartsEmptyWithoutConstructingElements)
{
    const BoundedVectorLifetimeCounts counts;
    {
        InplaceVector<BoundedVectorTrackedItem, 3> values;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.size(), 0u);
        EXPECT_EQ(values.capacity(), 3u);
        EXPECT_EQ(values.begin(), values.end());
        EXPECT_EQ(values.rbegin(), values.rend());
        EXPECT_EQ(counts.Constructed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 0);
}

TEST(InplaceVector, PushBackCopiesLvalue)
{
    InplaceVector<std::vector<int>, 1> values;
    std::vector<int> source{ 1, 2 };
    values.push_back(source);
    source[0] = 9;
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values.front(), (std::vector<int>{ 1, 2 }));
    EXPECT_EQ(source[0], 9);
}

TEST(InplaceVector, PushBackMovesRvalue)
{
    InplaceVector<std::unique_ptr<int>, 1> values;
    auto source = std::make_unique<int>(42);
    const int* address = source.get();
    values.push_back(std::move(source));
    EXPECT_EQ(source, nullptr);
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values.front().get(), address);
    EXPECT_EQ(*values.front(), 42);
}

TEST(InplaceVector, EmplaceSupportsImmovableElementsAndStableAddresses)
{
    BoundedVectorLifetimeCounts counts;
    InplaceVector<BoundedVectorTrackedItem, 3> values;
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

TEST(InplaceVector, AccessorsAllowMutationAndConstReading)
{
    InplaceVector<int, 3> values;
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

TEST(InplaceVector, IteratesForwardAndBackward)
{
    InplaceVector<int, 4> values;
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
    EXPECT_EQ((std::vector<int>(constValues.cbegin(), constValues.cend())),
        (std::vector<int>{ 10, 20, 30 }));
    EXPECT_EQ((std::vector<int>(constValues.rbegin(), constValues.rend())),
        (std::vector<int>{ 30, 20, 10 }));
}

TEST(InplaceVector, SupportsOverAlignedElements)
{
    struct alignas(128) Item
    {
        int Value;
    };
    InplaceVector<Item, 2> values;
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

TEST(InplaceVector, DestroysOnlyConstructedElements)
{
    BoundedVectorLifetimeCounts counts;
    {
        InplaceVector<BoundedVectorTrackedItem, 5> values;
        values.emplace_back(counts, 1);
        values.emplace_back(counts, 2);
        EXPECT_EQ(counts.Constructed, 2);
        EXPECT_EQ(counts.Destroyed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 2);
}

TEST(InplaceVector, RejectsInsertionBeyondCapacity)
{
    InplaceVector<int, 1> values;
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

TEST(InplaceVector, RejectsIndexOutsideConstructedElements)
{
    InplaceVector<int, 3> values;
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

TEST(InplaceVector, RejectsAccessToEmptyContainer)
{
    InplaceVector<int, 1> values;
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

TEST(BoundedVector, MovedFromContainerCanAllocateIndependentStorage)
{
    BoundedVector<int> source(2);
    source.emplace_back(10);
    BoundedVector<int> destination(std::move(source));
    // Inspecting and reusing the source is part of BoundedVector's move contract.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.capacity(), 2u);
    source.emplace_back(20);
    source.emplace_back(30);
    EXPECT_NE(source.data(), destination.data());
    EXPECT_EQ(source.size(), 2u);
    EXPECT_EQ(destination.size(), 1u);
    EXPECT_EQ(destination.front(), 10);
    EXPECT_EQ(source.front(), 20);
    EXPECT_EQ(source.back(), 30);

    BoundedVector<int> assigned(1);
    assigned = std::move(source);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.capacity(), 2u);
    source.emplace_back(40);
    source.emplace_back(50);
    EXPECT_NE(source.data(), assigned.data());
    EXPECT_EQ(assigned.front(), 20);
    EXPECT_EQ(assigned.back(), 30);
    EXPECT_EQ(source.front(), 40);
    EXPECT_EQ(source.back(), 50);
}

TEST(InplaceVector, MoveConstructionConstructsElementsInDestinationStorage)
{
    ContainerMoveCounts counts;
    {
        InplaceVector<ContainerMoveConstructOnly, 3> source;
        source.emplace_back(counts, 10);
        source.emplace_back(counts, 20);
        const auto* sourceAddress = source.data();
        const auto* firstResource = source.front().Value.get();
        {
            InplaceVector<ContainerMoveConstructOnly, 3> destination(std::move(source));
            ASSERT_EQ(destination.size(), 2u);
            EXPECT_EQ(destination.capacity(), 3u);
            EXPECT_NE(destination.data(), sourceAddress);
            EXPECT_EQ(destination.front().Value.get(), firstResource);
            ASSERT_NE(destination.back().Value, nullptr);
            EXPECT_EQ(*destination.back().Value, 20);
            // Inline moves leave live, moved-from elements in the source.
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
            EXPECT_EQ(source.size(), 2u);
            EXPECT_EQ(source.capacity(), 3u);
            EXPECT_EQ(source.data(), sourceAddress);
            EXPECT_EQ(source.front().Value, nullptr);
            EXPECT_EQ(source.back().Value, nullptr);
            EXPECT_EQ(counts.Constructed, 4);
            EXPECT_EQ(counts.Moved, 2);
            EXPECT_EQ(counts.Destroyed, 0);
        }
        EXPECT_EQ(counts.Destroyed, 2);
    }
    EXPECT_EQ(counts.Destroyed, 4);
}

TEST(InplaceVector, MoveConstructedElementsOutliveSource)
{
    ContainerMoveCounts counts;
    {
        auto destination = [&counts]()
        {
            InplaceVector<ContainerMoveConstructOnly, 2> source;
            source.emplace_back(counts, 42);
            // Construct a distinct result explicitly so NRVO cannot hide the move.
            return InplaceVector<ContainerMoveConstructOnly, 2>(std::move(source));
        }();
        EXPECT_EQ(counts.Constructed, 2);
        EXPECT_EQ(counts.Moved, 1);
        EXPECT_EQ(counts.Destroyed, 1);
        ASSERT_EQ(destination.size(), 1u);
        ASSERT_NE(destination.front().Value, nullptr);
        EXPECT_EQ(*destination.front().Value, 42);
    }
    EXPECT_EQ(counts.Destroyed, 2);
}

TEST(InplaceVector, MoveAssignmentReusesElementsAndDestroysExcess)
{
    CheckInplaceMoveAssignment(3, 1);
}

TEST(InplaceVector, MoveAssignmentReusesElementsAndConstructsExtras)
{
    CheckInplaceMoveAssignment(1, 3);
}

TEST(InplaceVector, MoveAssignmentOfEqualSizesOnlyAssigns)
{
    CheckInplaceMoveAssignment(2, 2);
}

TEST(InplaceVector, MoveAssignmentIntoEmptyContainerConstructsElements)
{
    CheckInplaceMoveAssignment(0, 3);
}

TEST(InplaceVector, MoveAssignmentFromEmptyContainerDestroysElements)
{
    CheckInplaceMoveAssignment(3, 0);
}

TEST(InplaceVector, MoveAssignmentBetweenEmptyContainersDoesNothing)
{
    CheckInplaceMoveAssignment(0, 0);
}

TEST(InplaceVector, SelfMovePreservesElements)
{
    ContainerMoveCounts counts;
    {
        InplaceVector<ContainerMoveAssignable, 2> values;
        values.emplace_back(counts, 42);
        const auto* address = values.data();
        auto& alias = values;
        values = std::move(alias);
        EXPECT_EQ(values.data(), address);
        ASSERT_EQ(values.size(), 1u);
        EXPECT_EQ(values.capacity(), 2u);
        ASSERT_NE(values.front().Value, nullptr);
        EXPECT_EQ(*values.front().Value, 42);
        EXPECT_EQ(counts.Moved, 0);
        EXPECT_EQ(counts.Assigned, 0);
        EXPECT_EQ(counts.Destroyed, 0);
    }
    EXPECT_EQ(counts.Destroyed, 1);
}

TEST(InplaceVector, EmptySourceCanBeMovedAndAssignedIntoPopulatedContainer)
{
    ContainerMoveCounts counts;
    {
        InplaceVector<ContainerMoveAssignable, 2> source;
        InplaceVector<ContainerMoveAssignable, 2> destination(std::move(source));
        EXPECT_TRUE(destination.empty());
        EXPECT_EQ(destination.capacity(), 2u);
        destination.emplace_back(counts, 10);
        destination.emplace_back(counts, 20);
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
        destination = std::move(source);
        EXPECT_TRUE(destination.empty());
        EXPECT_EQ(destination.capacity(), 2u);
        EXPECT_EQ(counts.Destroyed, 2);
        EXPECT_EQ(counts.Moved, 0);
        EXPECT_EQ(counts.Assigned, 0);
        destination.emplace_back(counts, 30);
        ASSERT_NE(destination.front().Value, nullptr);
        EXPECT_EQ(*destination.front().Value, 30);
    }
    EXPECT_EQ(counts.Constructed, 3);
    EXPECT_EQ(counts.Destroyed, 3);
}

TEST(BoundedVector, AppendRangePreservesOrderAndFillsCapacity)
{
    BoundedVector<int> values(4);
    CheckAppendRange(values);
}

TEST(InplaceVector, AppendRangePreservesOrderAndFillsCapacity)
{
    InplaceVector<int, 4> values;
    CheckAppendRange(values);
}

TEST(BoundedVector, AppendRangeRejectsOverflow)
{
    BoundedVector<int> values(2);
    values.emplace_back(1);
    const std::array input{ 2, 3 };
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.append_range(input);
        },
        "");
}

TEST(InplaceVector, AppendRangeRejectsOverflow)
{
    InplaceVector<int, 2> values;
    values.emplace_back(1);
    const std::array input{ 2, 3 };
    EXPECT_DEATH_IF_SUPPORTED(
        {
            IgnoreBoundedVectorAssertionDialogs();
            values.append_range(input);
        },
        "");
}

TEST(BoundedVector, EraseFirstMiddleAndLast)
{
    ContainerEraseCounts counts;
    {
        BoundedVector<ContainerEraseItem> values(5);
        CheckErasePositions(values, counts);
    }
    EXPECT_EQ(counts.Constructed, 5);
    EXPECT_EQ(counts.Destroyed, 5);
}

TEST(InplaceVector, EraseFirstMiddleAndLast)
{
    ContainerEraseCounts counts;
    {
        InplaceVector<ContainerEraseItem, 5> values;
        CheckErasePositions(values, counts);
    }
    EXPECT_EQ(counts.Constructed, 5);
    EXPECT_EQ(counts.Destroyed, 5);
}

TEST(BoundedVector, EraseRangeAndAllThenReinsert)
{
    ContainerEraseCounts counts;
    {
        BoundedVector<ContainerEraseItem> values(5);
        CheckEraseRangeAndReinsert(values, counts);
    }
    EXPECT_EQ(counts.Destroyed, 6);
}

TEST(InplaceVector, EraseRangeAndAllThenReinsert)
{
    ContainerEraseCounts counts;
    {
        InplaceVector<ContainerEraseItem, 5> values;
        CheckEraseRangeAndReinsert(values, counts);
    }
    EXPECT_EQ(counts.Destroyed, 6);
}

TEST(BoundedVector, EmptyEraseDoesNotAssignOrDestroyElements)
{
    ContainerEraseCounts counts;
    {
        BoundedVector<ContainerEraseItem> values(3);
        CheckEmptyErase(values, counts);
    }
    EXPECT_EQ(counts.Destroyed, 2);
}

TEST(InplaceVector, EmptyEraseDoesNotAssignOrDestroyElements)
{
    ContainerEraseCounts counts;
    {
        InplaceVector<ContainerEraseItem, 3> values;
        CheckEmptyErase(values, counts);
    }
    EXPECT_EQ(counts.Destroyed, 2);
}

// NOLINTEND(bugprone-use-after-move)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(readability-magic-numbers)
