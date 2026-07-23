#pragma once

#include "AssertHelper.h"

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

/// @brief  A fixed-size hash set for storing unique 64-bit values. It uses a simplified version of the
/// SwissTable algorithm.
class FixedSwissSet
{
    static_assert(sizeof(uint64_t) == sizeof(std::size_t), "FixedSwissSet requires 64-bit size_t");

public:
    static constexpr size_t kGroupSize = 1 << 4; // 16

    void Reset(const size_t maxItems)
    {
        // Keep the table at or below SwissTable's typical 7/8 load.
        const std::size_t requiredSlots = ((maxItems * 8) + 6) / 7;
        m_GroupCount = NextPowerOfTwo((requiredSlots + (kGroupSize - 1)) / kGroupSize);

        m_Controls.clear();
        m_Items.clear();

        m_Controls.assign(m_GroupCount * kGroupSize, Empty);
        m_Items.resize(m_GroupCount * kGroupSize);
    }

    // Returns true only when a new item is inserted.
    // Returns false when the item already exists or the table is full.
    bool Insert(const uint64_t item)
    {
        const uint64_t hash = Hash(item);
        const uint8_t tag = static_cast<uint8_t>(hash & 0x7f);
        const size_t firstGroup = static_cast<size_t>(hash >> 7) & (m_GroupCount - 1);

        for(size_t probe = 0; probe < m_GroupCount; ++probe)
        {
            const size_t group = (firstGroup + probe) & (m_GroupCount - 1);
            const size_t base = group * kGroupSize;

            // Check possible matches.
            for(size_t i = 0; i < kGroupSize; ++i)
            {
                const size_t slot = base + i;

                if(m_Controls[slot] == tag && m_Items[slot] == item)
                {
                    return false;
                }
            }

            // With no deletion, the first empty slot terminates the search.
            for(size_t i = 0; i < kGroupSize; ++i)
            {
                const size_t slot = base + i;

                if(m_Controls[slot] == Empty)
                {
                    if(m_Size == m_MaxItems)
                    {
                        return false;
                    }

                    m_Items[slot] = item;
                    m_Controls[slot] = tag;
                    ++m_Size;
                    return true;
                }
            }
        }

        return false;
    }

    bool Contains(const uint64_t item) const
    {
        const uint64_t hash = Hash(item);
        const uint8_t tag = static_cast<uint8_t>(hash & 0x7f);
        const size_t firstGroup = static_cast<size_t>(hash >> 7) & (m_GroupCount - 1);

        for(size_t probe = 0; probe < m_GroupCount; ++probe)
        {
            const size_t group = (firstGroup + probe) & (m_GroupCount - 1);
            const size_t base = group * kGroupSize;

            for(size_t i = 0; i < kGroupSize; ++i)
            {
                const size_t slot = base + i;

                if(m_Controls[slot] == tag && m_Items[slot] == item)
                {
                    return true;
                }
            }

            for(size_t i = 0; i < kGroupSize; ++i)
            {
                if(m_Controls[base + i] == Empty)
                {
                    return false;
                }
            }
        }

        return false;
    }

    std::size_t Size() const { return m_Size; }

private:
    static constexpr std::uint8_t Empty = 0x80;

    static std::size_t NextPowerOfTwo(std::size_t value)
    {
        std::size_t result = 1;

        while(result < value)
        {
            result *= 2;
        }

        return result;
    }

    /// @brief  Hashes a 64-bit value into another 64-bit value.
    /// It's the finalizer from the SplitMix64 PRNG, which is a fast and high-quality hash function.
    /// https://prng.di.unimi.it/splitmix64.c
    static uint64_t Hash(uint64_t value)
    {
        static constexpr uint64_t k1 = 0xbf58476d1ce4e5b9ULL;
        static constexpr uint64_t k2 = 0x94d049bb133111ebULL;
        static constexpr uint64_t kShift1 = 30;
        static constexpr uint64_t kShift2 = 27;
        static constexpr uint64_t kShift3 = 31;
        // SplitMix64 finalizer.
        value ^= value >> kShift1;
        value *= k1;
        value ^= value >> kShift2;
        value *= k2;
        value ^= value >> kShift3;
        return value;
    }

    std::vector<uint8_t> m_Controls;
    std::vector<uint64_t> m_Items;

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
        const size_t bodyIndex);

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
            size_t BodyIndex;
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
        size_t BodyIndex; // Index of the body occupying the cell.
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
    mutable FixedSwissSet m_UniquePairs;

    mutable bool m_NeedsSort{ true };
};