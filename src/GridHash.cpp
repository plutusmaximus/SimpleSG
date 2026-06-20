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

// Pack three 21-bit signed integers into a single 64-bit unsigned integer.
// FIXME (KB) - reduce range of Y (vertical).

constexpr uint64_t Bits21 = 21;
constexpr uint64_t Bits42 = 42;
constexpr uint64_t Mask21 = (1ull << Bits21) - 1;
constexpr int32_t MinValue21 = -(1 << (Bits21 - 1));
constexpr int32_t MaxValue21 = (1 << (Bits21 - 1)) - 1;

uint64_t
Pack3x21(const int32_t x, const int32_t y, const int32_t z)
{
    uint64_t ux = static_cast<uint64_t>(x);
    uint64_t uy = static_cast<uint64_t>(y);
    uint64_t uz = static_cast<uint64_t>(z);

    // Clamp values to the valid range for 21-bit signed integers.

    if(x < 0 && !MLG_VERIFY(x >= MinValue21, "x coordinate out of range: {}", x))
    {
        ux = static_cast<uint64_t>(MinValue21);
    }
    if(x > 0 && !MLG_VERIFY(x <= MaxValue21, "x coordinate out of range: {}", x))
    {
        ux = static_cast<uint64_t>(MaxValue21);
    }
    if(y < 0 && !MLG_VERIFY(y >= MinValue21, "y coordinate out of range: {}", y))
    {
        uy = static_cast<uint64_t>(MinValue21);
    }
    if(y > 0 && !MLG_VERIFY(y <= MaxValue21, "y coordinate out of range: {}", y))
    {
        uy = static_cast<uint64_t>(MaxValue21);
    }
    if(z < 0 && !MLG_VERIFY(z >= MinValue21, "z coordinate out of range: {}", z))
    {
        uz = static_cast<uint64_t>(MinValue21);
    }
    if(z > 0 && !MLG_VERIFY(z <= MaxValue21, "z coordinate out of range: {}", z))
    {
        uz = static_cast<uint64_t>(MaxValue21);
    }

    return (ux & Mask21) | ((uy & Mask21) << Bits21) | ((uz & Mask21) << Bits42);
}

int32_t
SignExtend21(const uint64_t v)
{
    uint64_t u = v & Mask21;

    if(u & (1ull << (Bits21 - 1)))
    {
        u |= ~Mask21;
    }

    return static_cast<int32_t>(u);
}

int32_t
UnpackX(const uint64_t v)
{
    return SignExtend21(v);
}

int32_t
UnpackY(const uint64_t v)
{
    return SignExtend21(v >> Bits21);
}

int32_t
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

Result<>
GridHash::Add(
    const Vec3f& bbMin, const Vec3f& bbMax, const Collider& collider, const size_t bodyIndex)
{
    MLG_ASSERT(m_NeedsSort,
        "Adding bodies after potential collisions have been generated. Is that intentional?");

    MLG_CHECKV(bbMin.x <= bbMax.x && bbMin.y <= bbMax.y && bbMin.z <= bbMax.z,
        "Invalid bounding box: Min: ({}, {}, {}), Max: ({}, {}, {})",
        bbMin.x,
        bbMin.y,
        bbMin.z,
        bbMax.x,
        bbMax.y,
        bbMax.z);

    const BoundingSphere& sphere = collider.GetEnclosingSphere();

    const Vec3f minExtent = bbMin + sphere.GetCenter() - Vec3f{sphere.GetRadius()};
    const Vec3f maxExtent = bbMax + sphere.GetCenter() + Vec3f{sphere.GetRadius()};

    const int32_t minX = Quantize(minExtent.x);
    const int32_t minY = Quantize(minExtent.y);
    const int32_t minZ = Quantize(minExtent.z);
    const int32_t maxX = Quantize(maxExtent.x);
    const int32_t maxY = Quantize(maxExtent.y);
    const int32_t maxZ = Quantize(maxExtent.z);

    const uint32_t dx = static_cast<uint32_t>(maxX - minX + 1);
    const uint32_t dy = static_cast<uint32_t>(maxY - minY + 1);
    const uint32_t dz = static_cast<uint32_t>(maxZ - minZ + 1);

    MLG_CHECK(AllocateItems(dx, dy, dz));

    for(int32_t x = minX; x <= maxX; ++x)
    {
        for(int32_t y = minY; y <= maxY; ++y)
        {
            for(int32_t z = minZ; z <= maxZ; ++z)
            {
                m_Items.emplace_back(Item::ItemParams{.BodyIndex = bodyIndex, .CellX = x, .CellY = y, .CellZ = z});
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
GridHash::AllocateItems(const uint32_t dx, const uint32_t dy, const uint32_t dz)
{
    const size_t dxs = static_cast<size_t>(dx);
    const size_t dys = static_cast<size_t>(dy);
    const size_t dzs = static_cast<size_t>(dz);

    MLG_CHECKV(dxs <= std::numeric_limits<size_t>::max() / dys,
        "Cell span overflow before multiply.");

    const size_t dxy = dxs * dys;

    MLG_CHECKV(dxy <= std::numeric_limits<size_t>::max() / dzs,
        "Cell span overflow before multiply.");

    // Number of cells the body potentially occupies.
    const size_t cellCount = dxy * dzs;

    MLG_CHECKV(cellCount <= kMaxCellsPerBody, "Too many cells occupied. count={}", cellCount);

    MLG_CHECKV(cellCount <= std::numeric_limits<size_t>::max() - m_Items.size(),
        "Grid reserve overflow. current={}, add={}",
        m_Items.size(),
        cellCount);

    m_Items.reserve(m_Items.size() + cellCount);

    return Result<>::Ok;
}

int32_t
GridHash::Quantize(const float value) const
{
    // Assume the value has been verified to be within the valid range in Add().
    return static_cast<int32_t>(std::floor(value * m_InvCellSize));
}

void
GridHash::Sort() const
{
    if(!m_NeedsSort || m_Items.empty())
    {
        return;
    }

    m_NeedsSort = false;

    std::ranges::sort(m_Items);

    m_PotentialCollisions.clear();

    // Generate body pairs for all bodies that share the same cell.

    for(size_t i = 0; i < m_Items.size(); ++i)
    {
        const size_t indexA = m_Items[i].BodyIndex;

        for(size_t j = i + 1; j < m_Items.size() && m_Items[j].CellCoords == m_Items[i].CellCoords; ++j)
        {
            // Bodies that share a cell are potentially colliding.

            const size_t indexB = m_Items[j].BodyIndex;
            if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell"))
            {
                m_PotentialCollisions.emplace_back(indexA, indexB);
            }
        }
    }

    if(m_PotentialCollisions.empty())
    {
        return;
    }

    // Sort to group duplicates together.
    std::ranges::sort(m_PotentialCollisions);

    auto itDst = m_PotentialCollisions.begin();
    const auto itEnd = m_PotentialCollisions.end();

    // Remove duplicate pairs.
    // Duplicates will be adjacent due to the sort above.
    for(auto itSrc = itDst + 1; itSrc != itEnd; ++itSrc)
    {
        if(*itDst == *itSrc)
        {
            // Skip duplicate pair.
            continue;
        }

        ++itDst;

        if(itSrc > itDst)
        {
            *itDst = *itSrc;
        }
    }

    const auto newEnd = itDst + 1;
    m_PotentialCollisions.erase(newEnd, m_PotentialCollisions.end());
}