#pragma once

#include "GridHash.h"
#include "Level.h"
#include "Result.h"
#include "VecMath.h"

#include <atomic>

class ThreadPool;

struct ImpactResult
{
    float Alpha; // Distance along path at impact, from 0 to 1.
    float PenetrationDepth;
    Vec3f ContactPoint;
    Vec3f ContactNormalBtoA; // Contact normal points from body B to body A.
    Vec3f PosAtImpactA;
    Vec3f PosAtImpactB;
};

struct SphereSweepParams
{
    Vec3f StartPosA{};
    Vec3f EndPosA{};
    BoundingSphere SphereA;

    Vec3f StartPosB{};
    Vec3f EndPosB{};
    BoundingSphere SphereB;
};

struct ImpactRecord
{
    BodyPair Bodies{0,0}; // Initialized to an invalid pair to catch uninitialized usage.

    SphereSweepParams SweepParams;

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

class PhysicsLevel
{
public:

    constexpr static size_t GRID_CELL_SIZE = 2;

    static Result<PhysicsLevel> Create(const Level& level, ThreadPool& threadPool);

    PhysicsLevel() = default;
    ~PhysicsLevel() = default;
    PhysicsLevel(const PhysicsLevel&) = delete;
    PhysicsLevel& operator=(const PhysicsLevel&) = delete;
    PhysicsLevel(PhysicsLevel&& other) = default;
    PhysicsLevel& operator=(PhysicsLevel&& other) = default;

    void AddForce(const size_t bodyIndex, const Vec3f& force);

    void Update(const float timeStep);

    Result<> SyncToLevel(Level& level);

    std::span<const Level::Node* const> GetNodes() const { return m_Nodes; }
    std::span<const RigidBody> GetBodies() const { return m_Bodies; }
    std::span<const Vec3f> GetPositions() const { return m_P0; }
    std::span<const Vec3f> GetLinearVelocities() const { return m_LinearVelocities; }

    void SetLinearVelocity(const size_t bodyIndex, const Vec3f& velocity)
    {
        if(MLG_VERIFY(bodyIndex < m_Bodies.size(), "Body index out of range"))
        {
            m_LinearVelocities[bodyIndex] = velocity;
        }
    }

private:

    // Represents a batch of sweep tests to be processed by a worker thread.
    // Many batches can be processed in parallel.
    struct SweepTestBatch
    {
        // Collection of pairs of bodies that potentially collide during the time step.
        std::span<ImpactRecord> PotentialImpacts;
        std::atomic<size_t>* FinishCounter{nullptr};

        static void Process(SweepTestBatch* batch);
    };

    PhysicsLevel(std::vector<const Level::Node*>&& nodes,
        std::vector<Vec3f>&& positions,
        std::vector<RigidBody>&& bodies,
        ThreadPool& threadPool)
        : m_Nodes(std::move(nodes)),
          m_Bodies(std::move(bodies)),
          m_ThreadPool(&threadPool)
    {
        m_PosPool[0] = std::move(positions);
        m_PosPool[1] = m_PosPool[0];    // Make a copy
        m_LinearVelocities.resize(m_Bodies.size(), Vec3f{ 0 });
        m_AccelerationPool[0] = m_LinearVelocities; // Make a copy
        m_AccelerationPool[1] = m_AccelerationPool[0]; // Make a copy
        m_ActiveBodies.resize(m_Bodies.size(), true);
        m_P0 = m_PosPool[0];
        m_P1 = m_PosPool[1];
        m_A0 = m_AccelerationPool[0];
        m_A1 = m_AccelerationPool[1];
    }

    void UpdateVelocities(const float dt);

    void PredictPositions(const float dt);

    void ResolveImpact(const ImpactRecord& impact);

    void FindAndResolveAllImpacts();

    void EnqueueSweepTests(SweepTestBatch* batch);

    static bool SphereSphereSweep(const SphereSweepParams& params, ImpactResult& impactResult);

    std::vector<const Level::Node*> m_Nodes;
    std::vector<Vec3f> m_PosPool[2];
    std::vector<Vec3f> m_LinearVelocities;
    std::vector<Vec3f> m_AccelerationPool[2];
    std::vector<RigidBody> m_Bodies;
    // Tracks which bodies are active in the current frame.
    std::vector<bool> m_ActiveBodies;

    std::vector<SweepTestBatch> m_SweepTestBatches;

    //Positions for the current frame.
    std::span<Vec3f> m_P0;
    //Predicted positions for the next frame.
    std::span<Vec3f> m_P1;
    //Accelerations for the current frame.
    std::span<Vec3f> m_A0;
    //Accelerations for the next frame.
    std::span<Vec3f> m_A1;

    GridHash m_GridHash{GRID_CELL_SIZE};

    std::vector<ImpactRecord> m_ImpactRecords;

    [[maybe_unused]] ThreadPool* m_ThreadPool{nullptr};
};