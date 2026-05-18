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
/// @tparam CELL_SIZE
template<size_t TP_CELL_SIZE>
requires (TP_CELL_SIZE > 0)
class GridHash
{
public:
    static constexpr size_t kCellSize = TP_CELL_SIZE;
    static constexpr float kInvCellSize = 1.0f / static_cast<float>(kCellSize);

    static constexpr int32_t kMaxExtent = std::numeric_limits<int32_t>::max() / 2;
    static constexpr int32_t kMinExtent = std::numeric_limits<int32_t>::min() / 2;
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
            "Bounding box exceeds maximum extents: Min: {}, Max: {}",
            minExtent,
            maxExtent);

        const int64_t minX = Quantize(minExtent.x);
        const int64_t minY = Quantize(minExtent.y);
        const int64_t minZ = Quantize(minExtent.z);
        const int64_t maxX = Quantize(maxExtent.x);
        const int64_t maxY = Quantize(maxExtent.y);
        const int64_t maxZ = Quantize(maxExtent.z);

        const int64_t dx64 = maxX - minX + 1;
        const int64_t dy64 = maxY - minY + 1;
        const int64_t dz64 = maxZ - minZ + 1;

        MLG_CHECK(AllocateCells(dx64, dy64, dz64));

        for(int64_t x = minX; x <= maxX; ++x)
        {
            for(int64_t y = minY; y <= maxY; ++y)
            {
                for(int64_t z = minZ; z <= maxZ; ++z)
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

    struct Cell
    {
        size_t BodyIndex; // Index of the body occupying the cell.
        std::array<int64_t, 3> Coords;// Cell coordinates (x, y, z)

        bool operator==(const Cell& that) const
        {
            return Coords == that.Coords && BodyIndex == that.BodyIndex;
        }

        bool operator!=(const Cell& that) const
        {
            return !(*this == that);
        }

        auto operator<=>(const Cell& that) const
        {
            if(Coords != that.Coords)
            {
                return Coords <=> that.Coords;
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
    Result<> AllocateCells(const int64_t dx64, const int64_t dy64, const int64_t dz64)
    {
        MLG_CHECKV(dx64 > 0 && dy64 > 0 && dz64 > 0,
            "Invalid cell span. dx: {}, dy: {}, dz: {}",
            dx64,
            dy64,
            dz64);

        const size_t dx = static_cast<size_t>(dx64);
        const size_t dy = static_cast<size_t>(dy64);
        const size_t dz = static_cast<size_t>(dz64);

        MLG_CHECKV(dx <= std::numeric_limits<size_t>::max() / dy,
            "Cell span overflow before multiply.");
        const size_t dxy = dx * dy;

        MLG_CHECKV(dxy <= std::numeric_limits<size_t>::max() / dz,
            "Cell span overflow before multiply.");
        const size_t cellCount = dxy * dz;

        MLG_CHECKV(cellCount <= kMaxCellsPerBody, "Too many cells occupied. count={}", cellCount);

        MLG_CHECKV(cellCount <= std::numeric_limits<size_t>::max() - m_Cells.size(),
            "Cell reserve overflow. current={}, add={}",
            m_Cells.size(),
            cellCount);

        m_Cells.reserve(m_Cells.size() + cellCount);

        return Result<>::Ok;
    }

    static int64_t Quantize(float value)
    {
        if(!MLG_VERIFY(value >= kMinExtent && value <= kMaxExtent,
            "Value out of range for quantization: {}. Valid range: [{}, {}]",
            value,
            kMinExtent,
            kMaxExtent))
        {
            return 0;
        }

        const float f = value * kInvCellSize;
        return static_cast<int64_t>(std::floor(f));
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