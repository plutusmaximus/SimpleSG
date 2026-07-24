#include "GridHash.h"
#include "BoundingVolumes.h"
#include "PerfMetrics.h"

#include <algorithm>


#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

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

////////// UniqueBodyPairSet //////////

UniqueBodyPairSet::UniqueBodyPairSet(const size_t initialSize)
    : m_MaxItems(initialSize)
{
    MLG_ABORTIF(initialSize > kMaximumItems,
        "UniqueBodyPairSet initial size is too large: {}",
        initialSize);

    // Keep the table at or below SwissTable's typical 7/8 load.
    const size_t requiredSlots = RequiredSlots(m_MaxItems);
    m_GroupCount = GroupCountPow2(requiredSlots);

    m_Controls.assign(m_GroupCount * kGroupSize, EmptyTag);
    m_Items.resize(m_GroupCount * kGroupSize);
}

void
UniqueBodyPairSet::Clear()
{
    std::ranges::fill(m_Controls, EmptyTag);
    m_Size = 0;
}

// Returns true only when a new item is inserted.
// Returns false when the item already exists or the table is full.
bool
UniqueBodyPairSet::Insert(const uint64_t item)
{
    while(true)
    {
        switch(InsertWithoutGrowth(item))
        {
            case InsertResult::Inserted:
                return true;
            case InsertResult::AlreadyPresent:
                return false;
            case InsertResult::Full:
                Grow();
                break;
        }
    }
}

bool
UniqueBodyPairSet::Contains(const uint64_t item) const
{
    const uint64_t hash = Hash(item);
    const uint8_t tag = static_cast<uint8_t>(hash & 0x7f);
    const size_t firstGroup = static_cast<size_t>(hash >> 7) & (m_GroupCount - 1);

    for(size_t probe = 0; probe < m_GroupCount; ++probe)
    {
        const size_t group = (firstGroup + probe) & (m_GroupCount - 1);
        const size_t base = group * kGroupSize;

        uint32_t matches = MatchControls(base, tag);
        while(matches != 0)
        {
            const size_t slot = base + static_cast<size_t>(std::countr_zero(matches));

            if(m_Items[slot].Value == item)
            {
                return true;
            }

            matches &= matches - 1;
        }

        if(MatchControls(base, EmptyTag) != 0)
        {
            return false;
        }
    }

    return false;
}

// private:

uint32_t
UniqueBodyPairSet::MatchControls(const size_t base, const uint8_t value) const
{
/*#if defined(__aarch64__)
    static constexpr size_t kHalfGroupSize = kGroupSize / 2;
    static constexpr std::array<uint8_t, kHalfGroupSize> bitValues{ 1, 2, 4, 8, 16, 32, 64, 128 };

    const uint8x16_t controls = vld1q_u8(&m_Controls[base]);
    // Compare each lane of the controls vector to the value, producing 0xff for matches and
    // 0x00 for non-matches.
    const uint8x16_t matches = vceqq_u8(controls, vdupq_n_u8(value));
    // Load the bit values for the lower eight lanes into a vector. Each lane corresponds to a
    // bit in the match mask.
    const uint8x8_t bits = vld1_u8(bitValues.data());

    // Convert each 0xff comparison result in the lower eight lanes into its corresponding
    // bit value, then sum the lanes to form bits 0-7 of the match mask.
    const uint32_t low = vaddv_u8(vand_u8(vget_low_u8(matches), bits));

    // Do the same for the upper eight lanes, which become bits 8-15 after the shift below.
    const uint32_t high = vaddv_u8(vand_u8(vget_high_u8(matches), bits));
    return low | (high << kHalfGroupSize);
#elif defined(__SSE2__)
    __m128i controls;
    std::memcpy(&controls, &m_Controls[base], sizeof(controls));
    const __m128i matches = _mm_cmpeq_epi8(controls, _mm_set1_epi8(static_cast<char>(value)));
    return static_cast<uint32_t>(_mm_movemask_epi8(matches));
#else*/

    uint32_t matches = 0;
    const std::span controls = std::span(m_Controls).subspan(base, kGroupSize);
    //VECTORIZE
    for(size_t i = 0; i < kGroupSize; ++i)
    {
        matches |= static_cast<uint32_t>(controls[i] == value) << i;
    }
    return matches;
//#endif
}

UniqueBodyPairSet::InsertResult
UniqueBodyPairSet::InsertWithoutGrowth(const uint64_t item)
{
    const uint64_t hash = Hash(item);
    const uint8_t tag = static_cast<uint8_t>(hash & 0x7f);
    const size_t firstGroup = static_cast<size_t>(hash >> 7) & (m_GroupCount - 1);

    for(size_t probe = 0; probe < m_GroupCount; ++probe)
    {
        const size_t group = (firstGroup + probe) & (m_GroupCount - 1);
        const size_t base = group * kGroupSize;

        uint32_t matches = MatchControls(base, tag);
        while(matches != 0)
        {
            const size_t slot = base + static_cast<size_t>(std::countr_zero(matches));

            if(m_Items[slot].Value == item)
            {
                return InsertResult::AlreadyPresent;
            }

            matches &= matches - 1;
        }

        // With no deletion, the first empty slot terminates the search.
        const uint32_t emptySlots = MatchControls(base, EmptyTag);
        if(emptySlots != 0)
        {
            if(m_Size == m_MaxItems)
            {
                return InsertResult::Full;
            }

            const size_t slot = base + static_cast<size_t>(std::countr_zero(emptySlots));
            m_Items[slot].Value = item;
            m_Controls[slot] = tag;
            ++m_Size;
            return InsertResult::Inserted;
        }
    }

    return InsertResult::Full;
}

void
UniqueBodyPairSet::Grow()
{
    MLG_ABORTIF(m_MaxItems > kMaximumItems / 2,
        "FixedSwissSet cannot grow beyond {} items",
        m_MaxItems);

    std::vector<uint8_t> oldControls = std::move(m_Controls);
    std::vector<ItemSlot> oldItems = std::move(m_Items);

    m_MaxItems = m_MaxItems == 0 ? 1 : m_MaxItems * 2;

    const size_t requiredSlots = RequiredSlots(m_MaxItems);
    m_GroupCount = GroupCountPow2(requiredSlots);

    m_Controls.assign(m_GroupCount * kGroupSize, EmptyTag);
    m_Items.resize(m_GroupCount * kGroupSize);
    m_Size = 0;

    for(size_t slot = 0; slot < oldControls.size(); ++slot)
    {
        if(oldControls[slot] != EmptyTag)
        {
            const InsertResult result = InsertWithoutGrowth(oldItems[slot].Value);
            MLG_ASSERT(result == InsertResult::Inserted, "Failed to rehash item while growing set");
            (void)result; // Silence unused variable warning in release builds.
        }
    }
}

size_t
UniqueBodyPairSet::GroupCountPow2(const size_t slotCount)
{
    const size_t groupCountRoundedUp =
        (slotCount / kGroupSize) + (slotCount % kGroupSize != 0 ? 1 : 0);

    size_t result = 1;

    while(result < groupCountRoundedUp)
    {
        result *= 2;
    }

    return result;
}

uint64_t
UniqueBodyPairSet::Hash(uint64_t value)
{
    // This is the finalizer from the SplitMix64 PRNG.
    // https://prng.di.unimi.it/splitmix64.c
    static constexpr uint64_t k1 = 0xbf58476d1ce4e5b9ULL;
    static constexpr uint64_t k2 = 0x94d049bb133111ebULL;
    static constexpr uint64_t kShift1 = 30;
    static constexpr uint64_t kShift2 = 27;
    static constexpr uint64_t kShift3 = 31;
    
    value ^= value >> kShift1;
    value *= k1;
    value ^= value >> kShift2;
    value *= k2;
    value ^= value >> kShift3;
    return value;
}

////////// GridHash::Item //////////

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

//////////// GridHash //////////

void
GridHash::Clear()
{
    m_Items.clear();
    m_PotentialCollisions.clear();
    m_UniquePairs.Clear();
    m_NeedsSort = true;
}

void
GridHash::Add(const Vec3f& p0,
    const Vec3f& p1,
    const BoundingSphere& boundingSphere,
    const uint32_t bodyIndex)
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

    const uint32_t dx = static_cast<uint32_t>(maxX) - static_cast<uint32_t>(minX) + 1;
    const uint32_t dy = static_cast<uint32_t>(maxY) - static_cast<uint32_t>(minY) + 1;
    const uint32_t dz = static_cast<uint32_t>(maxZ) - static_cast<uint32_t>(minZ) + 1;

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

    m_UniquePairs.Clear();

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
                    const uint64_t pairKey = (static_cast<uint64_t>(std::min(indexA, indexB)) << 32) | static_cast<uint64_t>(std::max(indexA, indexB));
                    if(m_UniquePairs.Insert(pairKey))
                    {
                        m_PotentialCollisions.emplace_back(indexA, indexB);
                    }
                }
            }
        }
    }

    if(m_PotentialCollisions.empty())
    {
        return;
    }
}