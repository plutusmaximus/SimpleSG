#pragma once

#include "GridHash.h"
#include "Level.h"
#include "Result.h"
#include "VecMath.h"

#include <algorithm>
#include <atomic>

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
    Vec3f StartPosA{};
    Vec3f EndPosA{};
    Collider ColliderA;

    Vec3f StartPosB{};
    Vec3f EndPosB{};
    Collider ColliderB;
};

struct ImpactRecord
{
    BodyPair Bodies{0,0}; // Initialized to an invalid pair to catch uninitialized usage.

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

class PhysicsLevel
{
public:

    constexpr static size_t GRID_CELL_SIZE = 2;

    static Result<> Create(const Level& level, PhysicsLevel& outPhysLevel);

    PhysicsLevel() = default;
    ~PhysicsLevel() = default;
    PhysicsLevel(const PhysicsLevel&) = delete;
    PhysicsLevel& operator=(const PhysicsLevel&) = delete;
    PhysicsLevel(PhysicsLevel&& other) = default;
    PhysicsLevel& operator=(PhysicsLevel&& other) = default;

    void AddForce(size_t bodyIndex, const Vec3f& force);

    void Update(const float timeStep);

    Result<> SyncToLevel(Level& level);

    std::span<const Level::Node* const> GetNodes() const { return m_Nodes; }
    std::span<const RigidBody> GetBodies() const { return m_Bodies; }
    std::span<const TrsTransformf> GetTransforms() const { return m_TrsCur; }
    std::span<Vec3f> GetLinearVelocities() { return m_LinearVelocities; }
    std::span<const Vec3f> GetLinearVelocities() const { return m_LinearVelocities; }
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

    PhysicsLevel(std::vector<const Level::Node*>&& nodes,
        std::vector<TrsTransformf>&& transforms,
        std::vector<RigidBody>&& bodies,
        std::vector<Collider>&& colliders)
        : m_Nodes(std::move(nodes)),
          m_Bodies(std::move(bodies)),
          m_Colliders(std::move(colliders))
    {
        m_TransformPool[0] = std::move(transforms);
        m_TransformPool[1] = m_TransformPool[0];    // Make a copy
        m_LinearVelocities.resize(m_Bodies.size(), Vec3f{ 0 });
        m_AccelerationPool[0].resize(m_Bodies.size(), Vec3f{ 0 });
        m_AccelerationPool[1].resize(m_Bodies.size(), Vec3f{ 0 });
        m_ActiveBodies.resize(m_Bodies.size(), true);
        m_TrsCur = m_TransformPool[0];
        m_TrsNext = m_TransformPool[1];
        m_AccelPrev = m_AccelerationPool[0];
        m_AccelCur = m_AccelerationPool[1];
    }

    void UpdateVelocities(const float dt);

    void PredictPositions(const float dt);

    void ResolveImpact(const ImpactRecord& impact);

    void FindAndResolveAllImpacts();

    static bool SphereSphereSweep(const ColliderSweepParams& params, ImpactResult& impactResult);

    std::vector<const Level::Node*> m_Nodes;
    std::vector<TrsTransformf> m_TransformPool[2];
    std::vector<Vec3f> m_LinearVelocities;
    std::vector<Vec3f> m_AccelerationPool[2];
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
    // Tracks which bodies are active in the current frame.
    std::vector<bool> m_ActiveBodies;

    std::vector<SweepTestBatch> m_SweepTestBatches;

    //Transforms for the current frame.
    std::span<TrsTransformf> m_TrsCur;
    //Predicted transforms for the next frame.
    std::span<TrsTransformf> m_TrsNext;
    //Accelerations for the prefvious frame.
    std::span<Vec3f> m_AccelPrev;
    //Accelerations for the current frame.
    std::span<Vec3f> m_AccelCur;

    GridHash m_GridHash{GRID_CELL_SIZE};

    std::vector<ImpactRecord> m_ImpactRecords;
};