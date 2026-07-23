#include "GridHash.h"
#include "BoundingVolumes.h"
#include "PerfMetrics.h"

#include <algorithm>
#include <unordered_set>

namespace
{
size_t ValidateCellSize(const size_t cellSize)
{
    MLG_ASSERT(cellSize > 0, "Cell size must be greater than 0");
    MLG_ASSERT(cellSize < 256, "Cell size must be less than 256");
    return cellSize;
}

// Pack three 21-bit signed integers into a single 64-bit unsigned integer.
// FIXME (KB) - reduce range of Y (vertical).

constexpr uint64_t Bits21 = 21;
constexpr uint64_t Bits42 = 42;
constexpr uint64_t Mask21 = (1ull << Bits21) - 1;
constexpr int32_t MinValue21 = -(1 << (Bits21 - 1));
constexpr int32_t MaxValue21 = (1 << (Bits21 - 1)) - 1;

constexpr uint64_t Clamp(const int32_t value)
{
    if(value < MinValue21)
    {
        return static_cast<uint64_t>(MinValue21);
    }
    if(value > MaxValue21)
    {
        return static_cast<uint64_t>(MaxValue21);
    }
    return static_cast<uint64_t>(value);
}

constexpr uint64_t
Pack3x21(const int32_t x, const int32_t y, const int32_t z)
{
    MLG_ASSERT(x >= MinValue21 && x <= MaxValue21, "x coordinate out of range: {}", x);
    MLG_ASSERT(y >= MinValue21 && y <= MaxValue21, "y coordinate out of range: {}", y);
    MLG_ASSERT(z >= MinValue21 && z <= MaxValue21, "z coordinate out of range: {}", z);

    const uint64_t ux = Clamp(x);
    const uint64_t uy = Clamp(y);
    const uint64_t uz = Clamp(z);

    return (ux & Mask21) | ((uy & Mask21) << Bits21) | ((uz & Mask21) << Bits42);
}

constexpr int32_t
SignExtend21(const uint64_t v)
{
    uint64_t u = v & Mask21;

    if(u & (1ull << (Bits21 - 1)))
    {
        u |= ~Mask21;
    }

    return static_cast<int32_t>(u);
}

constexpr int32_t
UnpackX(const uint64_t v)
{
    return SignExtend21(v);
}

constexpr int32_t
UnpackY(const uint64_t v)
{
    return SignExtend21(v >> Bits21);
}

constexpr int32_t
UnpackZ(const uint64_t v)
{
    return SignExtend21(v >> Bits42);
}
} // namespace

GridHash::Item::Item(const ItemParams& params)
    : CellCoords(Pack3x21(params.CellX, params.CellY, params.CellZ)),
      BodyIndex(params.BodyIndex)
{
}

int32_t
GridHash::Item::GetX() const
{
    return UnpackX(CellCoords);
}

int32_t
GridHash::Item::GetY() const
{
    return UnpackY(CellCoords);
}

int32_t
GridHash::Item::GetZ() const
{
    return UnpackZ(CellCoords);
}

GridHash::GridHash(const size_t cellSize)
    : m_CellSize(ValidateCellSize(cellSize)),
      m_InvCellSize(cellSize > 0 ? 1.0f / static_cast<float>(cellSize) : 0.0f)
{
}

void
GridHash::Clear()
{
    m_Items.clear();
    m_PotentialCollisions.clear();
    m_NeedsSort = true;
}

void
GridHash::Add(
    const Vec3f& p0, const Vec3f& p1, const BoundingSphere& boundingSphere, const size_t bodyIndex)
{
    MLG_SCOPED_TIMER("GridHash.Add");

    MLG_ASSERT(m_NeedsSort,
        "Adding bodies after potential collisions have been generated. Is that intentional?");

    const Vec3f vradius(boundingSphere.GetRadius());
    const Vec3f& center = boundingSphere.GetCenter();

    const Vec3f pmin =
        Vec3f(std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::min(p0.z, p1.z)) + center - vradius;
    const Vec3f pmax =
        Vec3f(std::max(p0.x, p1.x), std::max(p0.y, p1.y), std::max(p0.z, p1.z)) + center + vradius;

    const int32_t minX = Quantize(pmin.x);
    const int32_t minY = Quantize(pmin.y);
    const int32_t minZ = Quantize(pmin.z);
    const int32_t maxX = Quantize(pmax.x);
    const int32_t maxY = Quantize(pmax.y);
    const int32_t maxZ = Quantize(pmax.z);

    const size_t dx = static_cast<size_t>(maxX) - static_cast<size_t>(minX) + 1;
    const size_t dy = static_cast<size_t>(maxY) - static_cast<size_t>(minY) + 1;
    const size_t dz = static_cast<size_t>(maxZ) - static_cast<size_t>(minZ) + 1;

    AllocateItems(dx, dy, dz);

    Item::ItemParams params{ .BodyIndex = bodyIndex };

    for(params.CellX = minX; params.CellX <= maxX; ++params.CellX)
    {
        for(params.CellY = minY; params.CellY <= maxY; ++params.CellY)
        {
            for(params.CellZ = minZ; params.CellZ <= maxZ; ++params.CellZ)
            {
                m_Items.emplace_back(params);
            }
        }
    }

    m_NeedsSort = true;
}

size_t
GridHash::PotentialCollisionCount() const
{
    Sort();
    return m_PotentialCollisions.size();
}

GridHash::iterator
GridHash::begin()
{
    Sort();
    return m_PotentialCollisions.begin();
}

GridHash::iterator
GridHash::end()
{
    Sort();
    return m_PotentialCollisions.end();
}

// private:

void
GridHash::AllocateItems(const size_t dx, const size_t dy, const size_t dz)
{
    const size_t clampedDx = MLG_VERIFY(dx <= kMaxCellsPerDimension) ? dx : kMaxCellsPerDimension;
    const size_t clampedDy = MLG_VERIFY(dy <= kMaxCellsPerDimension) ? dy : kMaxCellsPerDimension;
    const size_t clampedDz = MLG_VERIFY(dz <= kMaxCellsPerDimension) ? dz : kMaxCellsPerDimension;

    const size_t dxy = clampedDx * clampedDy;

    // Number of cells the body potentially occupies.
    size_t cellCount = dxy * clampedDz;

    if(!MLG_VERIFY(cellCount <= kMaxCellsPerBody, "Too many cells occupied. count={}", cellCount))
    {
        cellCount = kMaxCellsPerBody;
    }

    if(!MLG_VERIFY(m_Items.size() <= std::numeric_limits<size_t>::max() - cellCount,
            "Grid reserve overflow. current={}, add={}",
            m_Items.size(),
            cellCount))
    {
        cellCount = std::numeric_limits<size_t>::max() - m_Items.size();
    }

    m_Items.reserve(m_Items.size() + cellCount);
}

int32_t
GridHash::Quantize(const float value) const
{
    // Assume the value has been verified to be within the valid range in Add().
    return static_cast<int32_t>(std::floor(value * m_InvCellSize));
}

#define METHOD 0

void
GridHash::Sort() const
{
    MLG_SCOPED_TIMER("GridHash.Sort");

    if(!m_NeedsSort || m_Items.empty())
    {
        return;
    }

    m_NeedsSort = false;

    // Sort the items by cell coordinates, so that all bodies in the same cell are adjacent.
    {
        MLG_SCOPED_TIMER("GridHash.Sort.Items");

        std::ranges::sort(m_Items);
    }

    m_PotentialCollisions.clear();

    // Generate body pairs for all bodies that share the same cell.

#if METHOD == 1
    std::unordered_set<uint64_t> uniquePairs;
#endif

    {
        MLG_SCOPED_TIMER("GridHash.Sort.GenerateBodyPairs");

        for(size_t i = 0; i < m_Items.size(); ++i)
        {
            const size_t indexA = m_Items[i].BodyIndex;

            for(size_t j = i + 1; j < m_Items.size() && m_Items[j].CellCoords == m_Items[i].CellCoords; ++j)
            {
                // Bodies that share a cell are potentially colliding.

                const size_t indexB = m_Items[j].BodyIndex;
                if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell"))
                {
#if METHOD == 0
                    m_PotentialCollisions.emplace_back(indexA, indexB);
#elif METHOD == 1                    
                    const uint64_t pairKey = (static_cast<uint64_t>(std::min(indexA, indexB)) << 32) |
                        static_cast<uint64_t>(std::max(indexA, indexB));
                    uniquePairs.emplace(pairKey);
#endif
                }
            }
        }
    }

#if METHOD == 0
    if(m_PotentialCollisions.empty())
#elif METHOD == 1
    if(uniquePairs.empty())
#endif
    {
        return;
    }

#if METHOD == 0
    // Sort to group duplicates together.
    {
        MLG_SCOPED_TIMER("GridHash.Sort.PotentialCollisions");

        std::ranges::sort(m_PotentialCollisions);
    }

    // Remove duplicates.
    {
        MLG_SCOPED_TIMER("GridHash.Sort.UniquePotentialCollisions");

        auto removed = std::ranges::unique(m_PotentialCollisions);
        m_PotentialCollisions.erase(removed.begin(), removed.end());
    }

#elif METHOD == 1

    for(const uint64_t pairKey : uniquePairs)
    {
        const size_t indexA = static_cast<size_t>(pairKey >> 32);
        const size_t indexB = static_cast<size_t>(pairKey & 0xFFFFFFFFull);
        m_PotentialCollisions.emplace_back(indexA, indexB);
    }
#endif
}