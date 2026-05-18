#pragma once

#include "Level.h"
#include "Result.h"
#include "VecMath.h"

#include <algorithm>

class BodyPair
{
public:

    BodyPair(size_t indexA, size_t indexB)
    {
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

    bool operator==(const BodyPair& that) const
    {
        return (m_IndexA == that.m_IndexA && m_IndexB == that.m_IndexB);
    }

    bool operator!=(const BodyPair& that) const
    {
        return !(*this == that);
    }

    auto operator<=>(const BodyPair& that) const
    {
        if(m_IndexA != that.m_IndexA)
        {
            return m_IndexA <=> that.m_IndexA;
        }

        return m_IndexB <=> that.m_IndexB;
    }

private:

    size_t m_IndexA;
    size_t m_IndexB;
};

struct ImpactResult
{
    float Alpha; // Distance along path at impact, from 0 to 1.
    float PenetrationDepth;
    Vec3f ContactPoint;
    Vec3f ContactNormalBtoA; // Contact normal points from body B to body A.
    Vec3f PosAtImpactA;
    Vec3f PosAtImpactB;
};

struct ImpactRecord
{
    class PhysicsSolver* Solver{nullptr}; //DO NOT SUBMIT
    BodyPair Bodies;
    ImpactResult Result;
    bool ImpactFound{false};

    bool operator==(const ImpactRecord& that) const
    {
        return Bodies == that.Bodies && Result.Alpha == that.Result.Alpha;
    }

    bool operator!=(const ImpactRecord& that) const
    {
        return !(*this == that);
    }

    auto operator<=>(const ImpactRecord& that) const
    {
        // We want to sort impact records by time of impact.

        if(ImpactFound != that.ImpactFound)
        {
            // Records with impacts should come before records without impacts.
            return ImpactFound ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        return std::strong_order(Result.Alpha, that.Result.Alpha);
    }
};

/// @brief  Spatial hash for broad-phase collision detection. Divides space into a grid of cells,
/// and hashes bodies into the cells they occupy.
/// @tparam CELL_SIZE_POW2
template<size_t CELL_SIZE_POW2>
class GridHash
{
    static_assert(CELL_SIZE_POW2 > 0, "CELL_SIZE_POW2 must be greater than 0");
    static_assert(CELL_SIZE_POW2 < 8, "CELL_SIZE_POW2 is >= 8 - was that intentional?");
public:
    static constexpr size_t kCellSize = 1 << CELL_SIZE_POW2;

    // 64 total bits in hash.
    // X/Z can consume 28 bits each.
    // Y can consume 8 bits.
    static constexpr int64_t kBitsX = 28;
    static constexpr int64_t kBitsY = 8;
    static constexpr int64_t kBitsZ = 28;
    static constexpr int64_t kMaxX = 1 << (kBitsX - 1);
    static constexpr int64_t kMinX = -kMaxX;
    static constexpr int64_t kMaxY = 1 << (kBitsY - 1);
    static constexpr int64_t kMinY = -kMaxY;
    static constexpr int64_t kMaxZ = 1 << (kBitsZ - 1);
    static constexpr int64_t kMinZ = -kMaxZ;
    static constexpr int64_t kBiasX = 1 << (kBitsX - 1);
    static constexpr int64_t kBiasY = 1 << (kBitsY - 1);
    static constexpr int64_t kBiasZ = 1 << (kBitsZ - 1);
    static constexpr int64_t kShiftZ = 0;
    static constexpr int64_t kShiftY = kShiftZ + kBitsZ;
    static constexpr int64_t kShiftX = kShiftY + kBitsY;

    // Arbitrary limit to prevent excessive cell counts for large bodies.
    static constexpr size_t kMaxCellsPerBody = 1000;

    GridHash() = default;
    GridHash(const GridHash&) = delete;
    GridHash& operator=(const GridHash&) = delete;
    GridHash(GridHash&&) = default;
    GridHash& operator=(GridHash&&) = default;

    /// @brief  Clears the grid hash, removing all bodies and potential collisions.
    void Clear()
    {
        m_Cells.clear();
        m_PotentialCollisions.clear();
        m_NeedsSort = false;
    }

    /// @brief  Adds a body to into the cells it occupies.
    /// @param bbMin The minimum corner of the body's bounding box.
    /// @param bbMax The maximum corner of the body's bounding box.
    /// @param collider The collider associated with the body.
    /// @param bodyIndex The index of the body.
    template<typename ColliderType>
    Result<> Add(
        const Vec3f& bbMin, const Vec3f& bbMax, const ColliderType& collider, const size_t bodyIndex)
    {
        MLG_CHECKV(bbMin.x <= bbMax.x && bbMin.y <= bbMax.y && bbMin.z <= bbMax.z,
            "Invalid bounding box: Min: {}, Max: {}",
            bbMin,
            bbMax);

        const float radius = collider.GetSphereRadius();
        MLG_CHECKV(radius >= 0, "Invalid collider radius: {}", radius);

        const Vec3f minExtent{bbMin.x - radius, bbMin.y - radius, bbMin.z - radius };
        const Vec3f maxExtent{bbMax.x + radius, bbMax.y + radius, bbMax.z + radius};

        MLG_CHECKV(minExtent.x >= kMinX && minExtent.y >= kMinY && minExtent.z >= kMinZ &&
                   maxExtent.x <= kMaxX && maxExtent.y <= kMaxY && maxExtent.z <= kMaxZ,
            "Bounding box exceeds maximum extents: Min: {}/{}, Max: {}/{}",
            minExtent,
            Vec3f{ kMinX, kMinY, kMinZ },
            maxExtent,
            Vec3f{ kMaxX, kMaxY, kMaxZ });

        const uint64_t minX = QuantizeX(minExtent.x);
        const uint64_t minY = QuantizeY(minExtent.y);
        const uint64_t minZ = QuantizeZ(minExtent.z);
        const uint64_t maxX = QuantizeX(maxExtent.x);
        const uint64_t maxY = QuantizeY(maxExtent.y);
        const uint64_t maxZ = QuantizeZ(maxExtent.z);

        const uint64_t dx = maxX - minX + 1;
        const uint64_t dy = maxY - minY + 1;
        const uint64_t dz = maxZ - minZ + 1;

        MLG_CHECK(AllocateCells(dx, dy, dz));

        for(uint64_t x = minX; x <= maxX; ++x)
        {
            for(uint64_t y = minY; y <= maxY; ++y)
            {
                for(uint64_t z = minZ; z <= maxZ; ++z)
                {
                    const uint64_t hash = (x << kShiftX) | (y << kShiftY) | (z << kShiftZ);
                    m_Cells.emplace_back(Cell{bodyIndex, hash});
                }
            }
        }

        m_NeedsSort = true;

        return Result<>::Ok;
    }

    using iterator = std::vector<BodyPair>::iterator;
    using const_iterator = std::vector<BodyPair>::const_iterator;

    size_t PotentialCollisionCount() const
    {
        Sort();
        return m_PotentialCollisions.size();
    }

    /// @brief Returns an iterator to the beginning of the range of unique body pairs that share a cell.
    iterator begin()
    {
        Sort();
        return m_PotentialCollisions.begin();
    }

    /// @brief Returns an iterator to the end of the range of unique body pairs that share a cell.
    iterator end()
    {
        Sort();
        return m_PotentialCollisions.end();
    }

private:

    struct Cell
    {
        size_t BodyIndex; // Index of the body occupying the cell.
        uint64_t Hash;

        bool operator==(const Cell& that) const
        {
            return Hash == that.Hash && BodyIndex == that.BodyIndex;
        }

        bool operator!=(const Cell& that) const
        {
            return !(*this == that);
        }

        auto operator<=>(const Cell& that) const
        {
            if(Hash != that.Hash)
            {
                return Hash <=> that.Hash;
            }

            return BodyIndex <=> that.BodyIndex;
        }
    };

    /// @brief Allocates the necessary number of cells for a body that spans the given number of
    /// cells in each dimension.
    /// @param dx The number of cells the body spans in the x dimension.
    /// @param dy The number of cells the body spans in the y dimension.
    /// @param dz The number of cells the body spans in the z dimension.
    /// @return
    Result<> AllocateCells(const uint64_t dx, const uint64_t dy, const uint64_t dz)
    {
        MLG_CHECKV(dx > 0 && dy > 0 && dz > 0,
            "Invalid cell span. dx: {}, dy: {}, dz: {}",
            dx,
            dy,
            dz);

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

    static uint64_t QuantizeX(float value)
    {
        if(!MLG_VERIFY(value >= kMinX && value <= kMaxX,
            "X Value out of range for quantization: {}. Valid range: [{}, {}]",
            value,
            kMinX,
            kMaxX))
        {
            return 0;
        }

        const int64_t i = static_cast<int64_t>(std::floor(value));

        return static_cast<uint64_t>((i + kBiasX) >> CELL_SIZE_POW2);
    }

    static uint64_t QuantizeZ(float value)
    {
        if(!MLG_VERIFY(value >= kMinZ && value <= kMaxZ,
            "Z Value out of range for quantization: {}. Valid range: [{}, {}]",
            value,
            kMinZ,
            kMaxZ))
        {
            return 0;
        }

        const int64_t i = static_cast<int64_t>(std::floor(value));

        return static_cast<uint64_t>((i + kBiasZ) >> CELL_SIZE_POW2);
    }

    static uint64_t QuantizeY(float value)
    {
        if(!MLG_VERIFY(value >= kMinY && value <= kMaxY,
            "Y Value out of range for quantization: {}. Valid range: [{}, {}]",
            value,
            kMinY,
            kMaxY))
        {
            return 0;
        }

        const int64_t i = static_cast<int64_t>(std::floor(value));

        return static_cast<uint64_t>((i + kBiasY) >> CELL_SIZE_POW2);
    }

    /// @brief Sorts the cells and generates the list of unique body pairs potentially colliding.
    void Sort() const
    {
        if(!m_NeedsSort)
        {
            return;
        }

        m_NeedsSort = false;

        std::sort(m_Cells.begin(), m_Cells.end());

        m_PotentialCollisions.clear();

        // For each cell generate body pairs for all bodies that share the cell.

        for(size_t i = 0; i < m_Cells.size(); ++i)
        {
            const size_t indexA = m_Cells[i].BodyIndex;

            for(size_t j = i + 1; j < m_Cells.size() && m_Cells[j].Hash == m_Cells[i].Hash; ++j)
            {
                // Bodies that share a cell are potentially colliding.

                const size_t indexB = m_Cells[j].BodyIndex;
                if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell"))
                {
                    m_PotentialCollisions.push_back({ indexA, indexB });
                }
            }
        }

        if(m_PotentialCollisions.empty())
        {
            return;
        }

        // Sort to group duplicates together.
        std::sort(m_PotentialCollisions.begin(), m_PotentialCollisions.end());

        size_t dst = 0;

        // Remove duplicate pairs.
        // Duplicates will be adjacent due to the sort above.
        for(size_t src = 1; src < m_PotentialCollisions.size(); ++src)
        {
            if(m_PotentialCollisions[dst] == m_PotentialCollisions[src])
            {
                //Skip duplicate pair.
                continue;
            }

            ++dst;

            if(src > dst)
            {
                m_PotentialCollisions[dst] = m_PotentialCollisions[src];
            }
        }

        m_PotentialCollisions.erase(m_PotentialCollisions.begin() + dst + 1,
            m_PotentialCollisions.end());
    }

    mutable std::vector<Cell> m_Cells;
    mutable std::vector<BodyPair> m_PotentialCollisions;

    mutable bool m_NeedsSort{false};
};

class PhysicsSolver
{
public:

    constexpr static size_t GRID_CELL_SIZE = 2;

    static Result<> Create(const Level& level, PhysicsSolver& outSolver);

    PhysicsSolver() = default;
    PhysicsSolver(const PhysicsSolver&) = delete;
    PhysicsSolver& operator=(const PhysicsSolver&) = delete;
    PhysicsSolver(PhysicsSolver&& other) = default;
    PhysicsSolver& operator=(PhysicsSolver&& other) = default;

    void AddForce(size_t bodyIndex, const Vec3f& force);

    void Update(const float timeStep);

    Result<> SyncToLevel(Level& level);

    float ComputeKineticEnergy() const;

    std::span<const Level::NodeHandle> GetNodeHandles() const { return m_NodeHandles; }
    std::span<const RigidBody> GetBodies() const { return m_Bodies; }
    std::span<const TrsTransformf> GetTransforms() const { return m_Trs0; }
    std::span<const Collider> GetColliders() const { return m_Colliders; }

private:

    PhysicsSolver(std::vector<Level::NodeHandle>&& nodeHandles,
        std::vector<TrsTransformf>&& transforms,
        std::vector<RigidBody>&& bodies,
        std::vector<Collider>&& colliders)
        : m_NodeHandles(std::move(nodeHandles)),
          m_Bodies(std::move(bodies)),
          m_Colliders(std::move(colliders))
    {
        m_TransformPool[0] = std::move(transforms);
        m_TransformPool[1] = m_TransformPool[0];
        m_AccelerationPool[0].resize(m_Bodies.size(), Vec3f{ 0 });
        m_AccelerationPool[1].resize(m_Bodies.size(), Vec3f{ 0 });
        m_ActiveBodies.resize(m_Bodies.size(), true);
        m_Trs0 = m_TransformPool[0];
        m_Trs1 = m_TransformPool[1];
        m_Am1 = m_AccelerationPool[0];
        m_A0 = m_AccelerationPool[1];
    }

    void UpdateVelocities(const float dt);

    void PredictPositions(const float dt);

    void ResolveImpact(const ImpactRecord& impact);

    void FindAndResolveAllImpacts();

    bool SphereSphereSweep(const BodyPair& bodyPair, ImpactResult& impactResult) const;

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<TrsTransformf> m_TransformPool[2];
    std::vector<Vec3f> m_AccelerationPool[2];
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
    // Tracks which bodies are active in the current frame.
    std::vector<bool> m_ActiveBodies;
    //Transforms for the current frame.
    std::span<TrsTransformf> m_Trs0;
    //Predicted transforms for the next frame.
    std::span<TrsTransformf> m_Trs1;
    //Accelerations for the last frame.
    std::span<Vec3f> m_Am1;
    //Predicted accelerations for the current frame.
    std::span<Vec3f> m_A0;

    GridHash<GRID_CELL_SIZE> m_GridHash;

    std::vector<ImpactRecord> m_ImpactRecords;
};