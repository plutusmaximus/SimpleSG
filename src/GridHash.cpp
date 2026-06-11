#include "GridHash.h"

#include <algorithm>

namespace
{
size_t ValidateCellSize(const size_t cellSize)
{
    MLG_ASSERT(cellSize > 0, "Cell size must be greater than 0");
    MLG_ASSERT(cellSize < 256, "Cell size must be less than 256");
    return cellSize;
}

constexpr uint32_t
Mix32(const uint32_t u)
{
    // From https://github.com/skeeto/hash-prospector
    constexpr uint32_t kHashParam1 = 0x7feb352dU;
    constexpr uint32_t kHashParam2 = 0x846ca68bU;
    constexpr size_t kShift16 = 16;
    constexpr size_t kShift15 = 15;

    uint32_t v = u;
    v ^= v >> kShift16;
    v *= kHashParam1;
    v ^= v >> kShift15;
    v *= kHashParam2;
    v ^= v >> kShift16;
    return v;
}

constexpr uint32_t
HashCell(const int32_t x, const int32_t y, const int32_t z)
{
    const uint32_t ux = static_cast<uint32_t>(x);
    const uint32_t uy = static_cast<uint32_t>(y);
    const uint32_t uz = static_cast<uint32_t>(z);

    constexpr uint32_t kSaltX = Mix32(0);
    constexpr uint32_t kSaltY = Mix32(1);
    constexpr uint32_t kSaltZ = Mix32(2);

    return Mix32(ux ^ kSaltX) ^ Mix32(uy ^ kSaltY) ^ Mix32(uz ^ kSaltZ);
}
} // namespace

GridHash::GridHash(const size_t cellSize)
    : m_CellSize(ValidateCellSize(cellSize)),
      m_InvCellSize(cellSize > 0 ? 1.0f / static_cast<float>(cellSize) : 0.0f)
{
}

void
GridHash::Clear()
{
    m_Cells.clear();
    m_PotentialCollisions.clear();
    m_NeedsSort = false;
}

Result<>
GridHash::Add(
    const Vec3f& bbMin, const Vec3f& bbMax, const Collider& collider, const size_t bodyIndex)
{
    MLG_CHECKV(bbMin.x <= bbMax.x && bbMin.y <= bbMax.y && bbMin.z <= bbMax.z,
        "Invalid bounding box: Min: ({}, {}, {}), Max: ({}, {}, {})",
        bbMin.x,
        bbMin.y,
        bbMin.z,
        bbMax.x,
        bbMax.y,
        bbMax.z);

    const float radius = collider.GetSphereRadius();
    MLG_CHECKV(radius >= 0, "Invalid collider radius: {}", radius);

    const Vec3f minExtent{ bbMin.x - radius, bbMin.y - radius, bbMin.z - radius };
    const Vec3f maxExtent{ bbMax.x + radius, bbMax.y + radius, bbMax.z + radius };

    const int32_t minX = Quantize(minExtent.x);
    const int32_t minY = Quantize(minExtent.y);
    const int32_t minZ = Quantize(minExtent.z);
    const int32_t maxX = Quantize(maxExtent.x);
    const int32_t maxY = Quantize(maxExtent.y);
    const int32_t maxZ = Quantize(maxExtent.z);

    const uint32_t dx = static_cast<uint32_t>(maxX - minX + 1);
    const uint32_t dy = static_cast<uint32_t>(maxY - minY + 1);
    const uint32_t dz = static_cast<uint32_t>(maxZ - minZ + 1);

    MLG_CHECK(AllocateCells(dx, dy, dz));

    for(int32_t x = minX; x <= maxX; ++x)
    {
        for(int32_t y = minY; y <= maxY; ++y)
        {
            for(int32_t z = minZ; z <= maxZ; ++z)
            {
                const Cell cell(bodyIndex, x, y, z);
                m_Cells.emplace_back(cell);
            }
        }
    }

    m_NeedsSort = true;

    return Result<>::Ok;
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

Result<>
GridHash::AllocateCells(const uint32_t dx, const uint32_t dy, const uint32_t dz)
{
    const size_t dxs = static_cast<size_t>(dx);
    const size_t dys = static_cast<size_t>(dy);
    const size_t dzs = static_cast<size_t>(dz);

    MLG_CHECKV(dxs <= std::numeric_limits<size_t>::max() / dys,
        "Cell span overflow before multiply.");

    const size_t dxy = dxs * dys;

    MLG_CHECKV(dxy <= std::numeric_limits<size_t>::max() / dzs,
        "Cell span overflow before multiply.");

    const size_t cellCount = dxy * dzs;

    MLG_CHECKV(cellCount <= kMaxCellsPerBody, "Too many cells occupied. count={}", cellCount);

    MLG_CHECKV(cellCount <= std::numeric_limits<size_t>::max() - m_Cells.size(),
        "Cell reserve overflow. current={}, add={}",
        m_Cells.size(),
        cellCount);

    m_Cells.reserve(m_Cells.size() + cellCount);

    return Result<>::Ok;
}

int32_t
GridHash::Quantize(const float value) const
{
    // Assume the value has been verified to be within the valid range in Add().
    return static_cast<int32_t>(std::floor(value * m_InvCellSize));
}

/// @brief Sorts the cells and generates the list of unique body pairs potentially colliding.
void
GridHash::Sort() const
{
    if(!m_NeedsSort)
    {
        return;
    }

    m_NeedsSort = false;

    std::ranges::sort(m_Cells);

    m_PotentialCollisions.clear();

    // For each cell generate body pairs for all bodies that share the cell.

    for(size_t i = 0; i < m_Cells.size(); ++i)
    {
        const size_t indexA = m_Cells[i].BodyIndex;

        for(size_t j = i + 1; j < m_Cells.size() && m_Cells[j].Coords == m_Cells[i].Coords; ++j)
        {
            // Bodies that share a cell are potentially colliding.

            const size_t indexB = m_Cells[j].BodyIndex;
            if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell"))
            {
                const BodyPair pair(indexA, indexB);
                m_PotentialCollisions.emplace_back(pair);
            }
        }
    }

    if(m_PotentialCollisions.empty())
    {
        return;
    }

    // Sort to group duplicates together.
    std::ranges::sort(m_PotentialCollisions);

    size_t dst = 0;

    // Remove duplicate pairs.
    // Duplicates will be adjacent due to the sort above.
    for(size_t src = 1; src < m_PotentialCollisions.size(); ++src)
    {
        if(m_PotentialCollisions[dst] == m_PotentialCollisions[src])
        {
            // Skip duplicate pair.
            continue;
        }

        ++dst;

        if(src > dst)
        {
            m_PotentialCollisions[dst] = m_PotentialCollisions[src];
        }
    }

    const auto newEnd = m_PotentialCollisions.begin() + static_cast<std::ptrdiff_t>(dst + 1);
    m_PotentialCollisions.erase(newEnd, m_PotentialCollisions.end());
}

GridHash::Cell::Coords::Coords(const int32_t cellX, const int32_t cellY, const int32_t cellZ)
    : Hash(HashCell(cellX, cellY, cellZ)),
      CellX(cellX),
      CellY(cellY),
      CellZ(cellZ)
{
}