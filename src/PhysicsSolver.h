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

/// @brief  An iterator that wraps another iterator and skips consecutive duplicate elements.
/// Requires that the underlying range is sorted, so that duplicates are adjacent.
/// @tparam T
template<typename T>
class UniqueIterator
{
public:
    UniqueIterator() = default;
    UniqueIterator(const UniqueIterator&) = default;
    UniqueIterator& operator=(const UniqueIterator&) = default;
    UniqueIterator(UniqueIterator&&) = default;
    UniqueIterator& operator=(UniqueIterator&&) = default;

    using iterator = std::vector<T>::const_iterator;

    explicit UniqueIterator(iterator iter, iterator end)
        : m_Iter(iter), m_End(end)
    {
    }

    UniqueIterator& operator++()
    {
        const T& current = *m_Iter;

        while(++m_Iter != m_End && *m_Iter == current)
        {
            // Skip duplicates.
        }

        return *this;
    }

    UniqueIterator operator++(int)
    {
        UniqueIterator copy = *this;
        ++*this;
        return copy;
    }

    const T& operator*() const
    {
        return *m_Iter;
    }

    const T* operator->() const
    {
        return &(*m_Iter);
    }

    bool operator==(const UniqueIterator& that) const
    {
        return m_Iter == that.m_Iter && m_End == that.m_End;
    }

    bool operator!=(const UniqueIterator& that) const
    {
        return !(*this == that);
    }

private:
    iterator m_Iter;
    iterator m_End;
};

template<size_t CELL_SIZE>
class GridHash
{
public:
    GridHash() = default;
    GridHash(const GridHash&) = delete;
    GridHash& operator=(const GridHash&) = delete;
    GridHash(GridHash&&) = default;
    GridHash& operator=(GridHash&&) = default;

    // Use UniqueIterator to skip duplicate body pairs that share multiple cells.
    using iterator = UniqueIterator<BodyPair>;

    void Clear()
    {
        m_Cells.clear();
        m_PotentialCollisions.clear();
        m_NeedsSort = false;
    }

    void Add(const Vec3f& p0, const Vec3f& p1, const Collider& collider, size_t bodyIndex)
    {
        const float radius = collider.GetSphereRadius();
        const Vec3f minExtent{p0.x - radius, p0.y - radius, p0.z - radius};
        const Vec3f maxExtent{p1.x + radius, p1.y + radius, p1.z + radius};

        const int64_t minX = Quantize(minExtent.x);
        const int64_t minY = Quantize(minExtent.y);
        const int64_t minZ = Quantize(minExtent.z);
        const int64_t maxX = Quantize(maxExtent.x);
        const int64_t maxY = Quantize(maxExtent.y);
        const int64_t maxZ = Quantize(maxExtent.z);

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
    }

    /// @brief Returns an iterator to the beginning of the range of unique body pairs that share a cell.
    iterator begin()
    {
        Sort();
        return iterator(m_PotentialCollisions.begin(), m_PotentialCollisions.end());
    }

    /// @brief Returns an iterator to the end of the range of unique body pairs that share a cell.
    iterator end()
    {
        Sort();
        return iterator(m_PotentialCollisions.end(), m_PotentialCollisions.end());
    }

private:

    struct Cell
    {
        size_t BodyIndex;
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

    static int64_t Quantize(float value)
    {
        static constexpr float INV_CELL_SIZE = 1.0f / static_cast<float>(CELL_SIZE);
        static constexpr float MAX_QUANTIZED_VALUE =
            static_cast<float>(std::numeric_limits<int64_t>::max());

        const float f = value * INV_CELL_SIZE;
        MLG_ASSERT(std::fabs(f) < MAX_QUANTIZED_VALUE, "Value out of range for quantization");
        return static_cast<int64_t>(std::floor(f));
    }

    void Sort() const
    {
        if(!m_NeedsSort)
        {
            return;
        }

        std::sort(m_Cells.begin(), m_Cells.end());

        m_PotentialCollisions.clear();

        for(size_t i = 0; i < m_Cells.size(); ++i)
        {
            const size_t indexA = m_Cells[i].BodyIndex;

            for(size_t j = i + 1; j < m_Cells.size() && m_Cells[j].Coords == m_Cells[i].Coords; ++j)
            {
                // Bodies that share a cell are potentially colliding.

                const size_t indexB = m_Cells[j].BodyIndex;
                if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell? Body index: {}", indexA))
                {
                    m_PotentialCollisions.push_back({ indexA, indexB });
                }
            }
        }

        // Sort potential collisions to group duplicates together, so they can be skipped by
        // UniqueIterator.
        std::sort(m_PotentialCollisions.begin(), m_PotentialCollisions.end());

        m_NeedsSort = false;
    }

    mutable std::vector<Cell> m_Cells;
    mutable std::vector<BodyPair> m_PotentialCollisions;

    mutable bool m_NeedsSort{false};
};

class PhysicsSolver
{
public:

    constexpr static size_t GRID_CELL_SIZE = 5;

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
        m_Trs0 = m_TransformPool[0];
        m_Trs1 = m_TransformPool[1];
        m_Am1 = m_AccelerationPool[0];
        m_A0 = m_AccelerationPool[1];
    }

    void UpdateVelocities(const float dt);

    void PredictPositions(const float dt);

    void ResolveImpact(const ImpactRecord& impact);

    void FindAndResolveAllImpacts();

    bool SphereSphereSweep(ImpactRecord& impactRecord) const;

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<TrsTransformf> m_TransformPool[2];
    std::vector<Vec3f> m_AccelerationPool[2];
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
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