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

    std::strong_ordering operator<=>(const BodyPair& that) const = default;

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
    static constexpr float kInvCellSize = 1.0f / static_cast<float>(kCellSize);

    static constexpr float kMinExtent = static_cast<float>(std::numeric_limits<int32_t>::min()) * kCellSize;
    static constexpr float kMaxExtent = static_cast<float>(std::numeric_limits<int32_t>::max()) * kCellSize;

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

        MLG_CHECKV(minExtent.x >= kMinExtent && minExtent.y >= kMinExtent && minExtent.z >= kMinExtent &&
                   maxExtent.x <= kMaxExtent && maxExtent.y <= kMaxExtent && maxExtent.z <= kMaxExtent,
            "Bounding box exceeds maximum extents: Min: {}/{}, Max: {}/{}",
            minExtent,
            Vec3f{ kMinExtent, kMinExtent, kMinExtent },
            maxExtent,
            Vec3f{ kMaxExtent, kMaxExtent, kMaxExtent });

        const int32_t minX = Quantize(minExtent.x);
        const int32_t minY = Quantize(minExtent.y);
        const int32_t minZ = Quantize(minExtent.z);
        const int32_t maxX = Quantize(maxExtent.x);
        const int32_t maxY = Quantize(maxExtent.y);
        const int32_t maxZ = Quantize(maxExtent.z);

        const uint32_t dx = maxX - minX + 1;
        const uint32_t dy = maxY - minY + 1;
        const uint32_t dz = maxZ - minZ + 1;

        MLG_CHECK(AllocateCells(dx, dy, dz));

        for(int32_t x = minX; x <= maxX; ++x)
        {
            for(int32_t y = minY; y <= maxY; ++y)
            {
                for(int32_t z = minZ; z <= maxZ; ++z)
                {
                    m_Cells.emplace_back(Cell{bodyIndex, x, y, z});
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

    class Cell
    {
    public:
        Cell(const size_t bodyIndex, const int32_t cellX, const int32_t cellY, const int32_t cellZ)
            : Hash(HashCell(cellX, cellY, cellZ)),
              CellX(cellX),
              CellY(cellY),
              CellZ(cellZ),

              BodyIndex(bodyIndex)
        {
        }

        Cell() = delete;
        Cell(const Cell&) = default;
        Cell& operator=(const Cell&) = default;
        Cell(Cell&&) = default;
        Cell& operator=(Cell&&) = default;

        constexpr static uint32_t Mix32(const uint32_t u)
        {
            // From https://github.com/skeeto/hash-prospector
            constexpr uint32_t kHashParam1 = 0x7feb352dU;
            constexpr uint32_t kHashParam2 = 0x846ca68bU;

            uint32_t v = u;
            v ^= v >> 16;
            v *= kHashParam1;
            v ^= v >> 15;
            v *= kHashParam2;
            v ^= v >> 16;
            return v;
        }

        constexpr static uint32_t HashCell(const int32_t x, const int32_t y, const int32_t z)
        {
            const uint32_t ux = uint32_t(x);
            const uint32_t uy = uint32_t(y);
            const uint32_t uz = uint32_t(z);

            constexpr uint32_t kSaltX = Mix32(0);
            constexpr uint32_t kSaltY = Mix32(1);
            constexpr uint32_t kSaltZ = Mix32(2);

            return Mix32(ux ^ kSaltX) ^ Mix32(uy ^ kSaltY) ^ Mix32(uz ^ kSaltZ);
        }

        // DO NOT change the order of the members below, as it affects the sort order and thus the
        // generation of body pairs.
        // Sorting by these fields (in this order) ensures bodies that share a cell are adjacent.
        // Within a cell bodies are sorted by index.
        uint32_t Hash;
        int32_t CellX, CellY, CellZ; // Quantized cell coordinates.
        size_t BodyIndex; // Index of the body occupying the cell.

        std::strong_ordering operator<=>(const Cell& that) const = default;
    };

    /// @brief Allocates the necessary number of cells for a body that spans the given number of
    /// cells in each dimension.
    /// @param dx The number of cells the body spans in the x dimension.
    /// @param dy The number of cells the body spans in the y dimension.
    /// @param dz The number of cells the body spans in the z dimension.
    /// @return
    Result<> AllocateCells(const uint32_t dx, const uint32_t dy, const uint32_t dz)
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

    static int32_t Quantize(const float value)
    {
        // Assume the value has been verified to be within the valid range in Add().
        return static_cast<int32_t>(std::floor(value * kInvCellSize));
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