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

struct ColliderSweepParams
{
    Vec3f StartPosA;
    Vec3f EndPosA;
    Collider ColliderA;

    Vec3f StartPosB;
    Vec3f EndPosB;
    Collider ColliderB;
};

struct ImpactRecord
{
    BodyPair Bodies;

    ColliderSweepParams SweepParams;

    ImpactResult Result{};

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
        if(ImpactFound != that.ImpactFound)
        {
            // Default ordering of bool would put records without impacts before records with
            // impacts.
            // We want records with impacts to come before records without impacts.
            return ImpactFound ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        // For records with impacts, sort by time of impact (Alpha).
        return std::strong_order(Result.Alpha, that.Result.Alpha);
    }
};

/// @brief  Spatial hash for broad-phase collision detection. Divides space into a grid of cells,
/// and hashes bodies into the cells they occupy.
class GridHash
{
public:
    // Arbitrary limit to prevent excessive cell counts for large bodies.
    static constexpr size_t kMaxCellsPerBody = 1000;

    explicit GridHash(const size_t cellSize)
    : m_CellSize(cellSize)
    {
        MLG_ASSERT(cellSize > 0, "Cell size must be greater than 0");
        MLG_ASSERT(cellSize < 256, "Cell size is >= 256 - was that intentional?");

        m_InvCellSize = 1.0f / static_cast<float>(cellSize);
    }

    GridHash() = delete;
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

    size_t GetCellSize() const { return m_CellSize; }

    /// @brief  Adds a body to into the cells it occupies.
    /// @param bbMin The minimum corner of the body's bounding box.
    /// @param bbMax The maximum corner of the body's bounding box.
    /// @param collider The collider associated with the body.
    /// @param bodyIndex The index of the body.
    template<typename ColliderType>
    Result<> Add(const Vec3f& bbMin,
        const Vec3f& bbMax,
        const ColliderType& collider,
        const size_t bodyIndex)
    {
        MLG_CHECKV(bbMin.x <= bbMax.x && bbMin.y <= bbMax.y && bbMin.z <= bbMax.z,
            "Invalid bounding box: Min: {}, Max: {}",
            bbMin,
            bbMax);

        const float radius = collider.GetSphereRadius();
        MLG_CHECKV(radius >= 0, "Invalid collider radius: {}", radius);

        const Vec3f minExtent{bbMin.x - radius, bbMin.y - radius, bbMin.z - radius };
        const Vec3f maxExtent{bbMax.x + radius, bbMax.y + radius, bbMax.z + radius};

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
            : Coords{cellX, cellY, cellZ},
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

        struct CellCoords
        {
            CellCoords(const int32_t cellX, const int32_t cellY, const int32_t cellZ)
                : Hash(HashCell(cellX, cellY, cellZ)),
                  CellX(cellX),
                  CellY(cellY),
                  CellZ(cellZ)
            {
            }

            uint32_t Hash;
            int32_t CellX, CellY, CellZ; // Quantized cell coordinates.

            bool operator==(const CellCoords& that) const
            {
                return Hash == that.Hash && CellX == that.CellX && CellY == that.CellY &&
                       CellZ == that.CellZ;
            }

            std::strong_ordering operator<=>(const CellCoords& that) const
            {
                std::strong_ordering order = Hash <=> that.Hash;
                if(order != std::strong_ordering::equal)
                {
                    return order;
                }

                order = CellX <=> that.CellX;
                if(order != std::strong_ordering::equal)
                {
                    return order;
                }

                order = CellY <=> that.CellY;
                if(order != std::strong_ordering::equal)
                {
                    return order;
                }

                return CellZ <=> that.CellZ;
            }
        };

        CellCoords Coords;
        size_t BodyIndex; // Index of the body occupying the cell.

        std::strong_ordering operator<=>(const Cell& that) const
        {
            std::strong_ordering order = Coords <=> that.Coords;
            if(order != std::strong_ordering::equal)
            {
                return order;
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

    int32_t Quantize(const float value) const
    {
        // Assume the value has been verified to be within the valid range in Add().
        return static_cast<int32_t>(std::floor(value * m_InvCellSize));
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

            for(size_t j = i + 1; j < m_Cells.size() && m_Cells[j].Coords == m_Cells[i].Coords; ++j)
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

        const std::vector<BodyPair>::iterator newEnd =
            m_PotentialCollisions.begin() + std::vector<BodyPair>::difference_type(dst + 1);
        m_PotentialCollisions.erase(newEnd, m_PotentialCollisions.end());
    }

    size_t m_CellSize;
    float m_InvCellSize;

    mutable std::vector<Cell> m_Cells;
    mutable std::vector<BodyPair> m_PotentialCollisions;

    mutable bool m_NeedsSort{false};
};

class PhysicsLevel
{
public:

    constexpr static size_t GRID_CELL_SIZE = 2;

    static Result<> Create(const Level& level, PhysicsLevel& outPhysLevel);

    PhysicsLevel() = default;
    PhysicsLevel(const PhysicsLevel&) = delete;
    PhysicsLevel& operator=(const PhysicsLevel&) = delete;
    PhysicsLevel(PhysicsLevel&& other) = default;
    PhysicsLevel& operator=(PhysicsLevel&& other) = default;

    void AddForce(size_t bodyIndex, const Vec3f& force);

    void Update(const float timeStep);

    Result<> SyncToLevel(Level& level);

    float ComputeKineticEnergy() const;

    std::span<const Level::NodeHandle> GetNodeHandles() const { return m_NodeHandles; }
    std::span<const RigidBody> GetBodies() const { return m_Bodies; }
    std::span<const TrsTransformf> GetTransforms() const { return m_Trs0; }
    std::span<const Collider> GetColliders() const { return m_Colliders; }

private:

    // Represents a batch of sweep tests to be processed by a worker thread.
    // Many batches can be processed in parallel.
    struct SweepTestBatch
    {
        // Collection of pairs of bodies that potentially collide during the time step.
        std::span<ImpactRecord> PotentialImpacts;
        std::atomic<size_t>* FinishCounter{nullptr};

        void Enqueue();

        static void Process(SweepTestBatch* batch);
    };

    PhysicsLevel(std::vector<Level::NodeHandle>&& nodeHandles,
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

    static bool SphereSphereSweep(const ColliderSweepParams& params, ImpactResult& impactResult);

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<TrsTransformf> m_TransformPool[2];
    std::vector<Vec3f> m_AccelerationPool[2];
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
    // Tracks which bodies are active in the current frame.
    std::vector<bool> m_ActiveBodies;

    std::vector<SweepTestBatch> m_SweepTestBatches;

    //Transforms for the current frame.
    std::span<TrsTransformf> m_Trs0;
    //Predicted transforms for the next frame.
    std::span<TrsTransformf> m_Trs1;
    //Accelerations for the last frame.
    std::span<Vec3f> m_Am1;
    //Accelerations for the current frame.
    std::span<Vec3f> m_A0;

    GridHash m_GridHash{GRID_CELL_SIZE};

    std::vector<ImpactRecord> m_ImpactRecords;
};