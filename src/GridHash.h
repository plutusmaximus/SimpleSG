#pragma once

#include "AssertHelper.h"

#include <limits>
#include <vector>

class BoundingSphere;
template<typename T>
class Vec3;
using Vec3f = Vec3<float>;

class BodyPair
{
public:
    BodyPair() = delete;

    BodyPair(const size_t indexA, const size_t indexB)
    {
        MLG_ASSERT(indexA != indexB, "BodyPair cannot contain the same body twice");

        // Ensure the pair is always stored in a consistent order.
        // This enables identifying and skipping duplicate pairs.
        if(indexA < indexB)
        {
            m_IndexA = indexA;
            m_IndexB = indexB;
        }
        else
        {
            m_IndexA = indexB;
            m_IndexB = indexA;
        }
    }

    size_t IndexA() const { return m_IndexA; }
    size_t IndexB() const { return m_IndexB; }

    std::strong_ordering operator<=>(const BodyPair& that) const = default;

private:
    size_t m_IndexA;
    size_t m_IndexB;
};

/// @brief A hash set that stores unique body pairs using a SwissTable-like structure.
/// See https://abseil.io/about/design/swisstables
class UniqueBodyPairSet
{
public:
    // The number of control bytes in a group. Each group contains 16 control bytes, which are
    // processed together using SIMD instructions.
    static constexpr size_t kGroupSize = 1 << 4;

    // Maintain a 7/8 load factor to avoid excessive probing.  See calculation in RequiredSlots()
    // for details.  Therefore we need to set the largest item count for which ((itemCount * 8) + 6)
    // can be calculated without overflow.
    static constexpr size_t kMaximumItems = (std::numeric_limits<size_t>::max() - 6) / 8;

    explicit UniqueBodyPairSet(const size_t initialSize);

    /// @brief  Removes all items from the set.
    void Clear();
    
    /// @brief  Inserts an item into the set, growing the set if necessary.
    bool Insert(const uint64_t item);

    /// @brief  Returns true if the set contains the given item.
    bool Contains(const uint64_t item) const;

    size_t Size() const { return m_Size; }

    bool Empty() const { return m_Size == 0; }

private:
    static constexpr uint8_t EmptyTag = 0x80;

    struct ItemSlot
    {
        // Value is initialized only when the corresponding control byte becomes occupied.
        ItemSlot() noexcept // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
        {
        } 

        uint64_t Value;
    };

    enum class InsertResult
    {
        Inserted,
        AlreadyPresent,
        Full
    };

    /// @brief  Returns a bitmask of the control bytes in the group that match the given value.
    uint32_t MatchControls(const size_t base, const uint8_t value) const;

    /// @brief  Inserts an item into the set without growing the set.  Returns InsertResult::Full if
    /// the set is full and needs to be grown.
    InsertResult InsertWithoutGrowth(const uint64_t item);

    /// @brief  Grows the set to accommodate more items.  Rehashes all existing items into the new
    /// set.
    void Grow();

    /// @brief  Returns the number of slots required to store the given number of items, keeping the
    /// load factor at or below 7/8.
    static size_t RequiredSlots(const size_t itemCount)
    {
        // Keep the table at or below SwissTable's typical 7/8 load.
        return ((itemCount * 8) + 6) / 7; // NOLINT(readability-magic-numbers)
    }

    /// @brief  Computes the power of 2 group count from the given slot count.
    static size_t GroupCountPow2(const size_t slotCount);

    /// @brief  Hashes a 64-bit value into another 64-bit value.
    static uint64_t Hash(uint64_t value);

    // The control bytes for each group.
    std::vector<uint8_t> m_Controls;
    // The items.
    std::vector<ItemSlot> m_Items;

    size_t m_GroupCount = 0;
    size_t m_MaxItems = 0;
    size_t m_Size = 0;
};

/// @brief  Spatial hash for broad-phase collision detection. Divides space into a grid of cells,
/// and hashes bodies into the cells they occupy.
class GridHash
{
public:
    // Arbitrary limit to prevent excessive cell counts for large bodies.
    static constexpr size_t kMaxCellsPerBody = 1000;
    static constexpr size_t kMaxCellsPerDimension = 100;

    GridHash() = delete;
    ~GridHash() = default;
    GridHash(const GridHash&) = delete;
    GridHash& operator=(const GridHash&) = delete;
    GridHash(GridHash&&) = default;
    GridHash& operator=(GridHash&&) = default;

    explicit GridHash(const size_t cellSize);

    /// @brief  Clears the grid hash, removing all bodies and potential collisions.
    void Clear();

    size_t GetCellSize() const { return m_CellSize; }

    /// @brief  Adds a body to into the grid cells it occupies.
    /// @param p0 One corner of the body's bounding box.
    /// @param p1 The other corner of the body's bounding box.
    /// @param boundingSphere The bounding sphere associated with the body.
    /// @param bodyIndex The index of the body.
    void Add(const Vec3f& p0,
        const Vec3f& p1,
        const BoundingSphere& boundingSphere,
        const uint32_t bodyIndex);

    using iterator = std::vector<BodyPair>::iterator;
    using const_iterator = std::vector<BodyPair>::const_iterator;

    size_t PotentialCollisionCount() const;

    /// @brief Returns an iterator to the beginning of the range of unique body pairs that share a
    /// cell.
    iterator begin();

    /// @brief Returns an iterator to the end of the range of unique body pairs that share a cell.
    iterator end();

private:
    class Item
    {
    public:
        Item() = delete;

        struct ItemParams
        {
            uint32_t BodyIndex;
            int32_t CellX;
            int32_t CellY;
            int32_t CellZ;
        };

        explicit Item(const ItemParams& params);

        int32_t GetX() const;
        int32_t GetY() const;
        int32_t GetZ() const;

        friend bool operator==(const Item& a, const Item& b)
        {
            return a.CellCoords == b.CellCoords && a.BodyIndex == b.BodyIndex;
        }

        friend auto operator<=>(const Item& a, const Item& b)
        {
            if(a.CellCoords != b.CellCoords)
            {
                return a.CellCoords <=> b.CellCoords;
            }

            return a.BodyIndex <=> b.BodyIndex;
        }

        uint64_t CellCoords;
        uint32_t BodyIndex; // Index of the body occupying the cell.
    };

    /// @brief Allocates the necessary number of items for a body that spans the given number of
    /// cells in each dimension.
    /// @param dx The number of cells the body spans in the x dimension.
    /// @param dy The number of cells the body spans in the y dimension.
    /// @param dz The number of cells the body spans in the z dimension.
    /// @return
    void AllocateItems(const size_t dx, const size_t dy, const size_t dz);

    int32_t Quantize(const float value) const;

    /// @brief Sorts the cells and generates the list of unique body pairs potentially colliding.
    void Sort() const;

    size_t m_CellSize;
    float m_InvCellSize;

    mutable std::vector<Item> m_Items;
    mutable std::vector<BodyPair> m_PotentialCollisions;

    static constexpr size_t kInitialUniquePairsSize = 1024;
    mutable UniqueBodyPairSet m_UniquePairs{ kInitialUniquePairsSize };

    mutable bool m_NeedsSort{ true };
};
