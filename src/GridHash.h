#pragma once

#include "PhysicsTypes.h"
#include "Result.h"

#include <vector>

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

/// @brief  Spatial hash for broad-phase collision detection. Divides space into a grid of cells,
/// and hashes bodies into the cells they occupy.
class GridHash
{
public:
    // Arbitrary limit to prevent excessive cell counts for large bodies.
    static constexpr size_t kMaxCellsPerBody = 1000;

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
    /// @param bbMin The minimum corner of the body's bounding box.
    /// @param bbMax The maximum corner of the body's bounding box.
    /// @param collider The collider associated with the body.
    /// @param bodyIndex The index of the body.
    Result<> Add(
        const Vec3f& bbMin, const Vec3f& bbMax, const Collider& collider, const size_t bodyIndex);

    using iterator = std::vector<BodyPair>::iterator;
    using const_iterator = std::vector<BodyPair>::const_iterator;

    size_t PotentialCollisionCount() const;

    /// @brief Returns an iterator to the beginning of the range of unique body pairs that share a cell.
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
    Result<> AllocateItems(const uint32_t dx, const uint32_t dy, const uint32_t dz);

    int32_t Quantize(const float value) const;

    /// @brief Sorts the cells and generates the list of unique body pairs potentially colliding.
    void Sort() const;

    size_t m_CellSize;
    float m_InvCellSize;

    mutable std::vector<Item> m_Items;
    mutable std::vector<BodyPair> m_PotentialCollisions;

    mutable bool m_NeedsSort{true};
};