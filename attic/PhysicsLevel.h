#pragma once

#define PL_V1 0
#if PL_V1

#include "GridHash.h"
#include "Level.h"
#include "Result.h"
#include "VecMath.h"

struct ImpactResult
{
    float Alpha; // Distance along path at impact, from 0 to 1.
    Vec3f ContactPoint;
    Vec3f ContactNormalBtoA; // Contact normal points from body B to body A.
    Vec3f PosAtImpactA;
    Vec3f PosAtImpactB;
};

struct ImpactRecord // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    size_t BodyIndexA;
    size_t BodyIndexB;

    // Sum of the inverse masses of the two bodies.
    float InvMassSum;

    // Reciprocal of the sum of the inverse masses of the two bodies.
    float RecipInvMassSum;

    ImpactResult Result;

    bool ImpactFound{false};

    bool operator==(const ImpactRecord& that) const
    {
        return BodyIndexA == that.BodyIndexA
            && BodyIndexB == that.BodyIndexB
            && Result.Alpha == that.Result.Alpha;
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

    static Result<PhysicsLevel> Create(const Level& level);

    PhysicsLevel() = default;
    ~PhysicsLevel() = default;
    PhysicsLevel(const PhysicsLevel&) = delete;
    PhysicsLevel& operator=(const PhysicsLevel&) = delete;
    PhysicsLevel(PhysicsLevel&& other) = default;
    PhysicsLevel& operator=(PhysicsLevel&& other) = default;

    void PredictPositions(const float dt);

    void Resolve();

    void ApplyImpulse(const LevelNode* node, const Vec3f& impulse);

    void AddForce(const LevelNode* node, const Vec3f& force);

    void UpdateVelocities(const float dt);

    Result<> SyncToLevel(Level& level);

    size_t GetNodeCount() const { return m_Nodes.size(); }

    std::span<const LevelNode* const> GetNodes() const { return m_Nodes; }
    Vec3f GetPosition(const LevelNode* node) const;
    Vec3f GetLinearVelocity(const LevelNode* node) const;
    float GetRadius(const LevelNode* node) const;
    float GetInverseMass(const LevelNode* node) const;
    void GetPositions(VVec3& positions) const;
    void GetLinearVelocities(VVec3& linearVelocities) const;
    void GetInverseMasses(std::span<float>& invMasses) const;

    void SetLinearVelocity(const LevelNode* node, const Vec3f& velocity);

    // Radians per second
    void SetAngularVelocity(const LevelNode* node, const Vec3f& angularVelocity);

private:

    /// Mapping between a node and its index into the various arrays below.
    /// We maintain a sorted vector of NodeAndIndex so we can quickly find
    /// the index of a node using binary search.
    class NodeAndIndex
    {
    public:
        static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
        
        NodeAndIndex() = delete;

        NodeAndIndex(const LevelNode* node, size_t index)
            : m_Node(node), m_Index(index)
        {
        }

        const LevelNode* GetNode() const { return m_Node; }
        size_t GetIndex() const { return m_Index; }

        friend auto operator<=>(const NodeAndIndex& a, const NodeAndIndex& b)
        {
            return a.m_Node <=> b.m_Node;
        }

        friend bool operator==(const NodeAndIndex& lhs, const NodeAndIndex& rhs) = default;
        friend bool operator!=(const NodeAndIndex& lhs, const NodeAndIndex& rhs) = default;

    private:
        friend class PhysicsLevel;

        const LevelNode* m_Node{nullptr};
        size_t m_Index{0};
    };

    explicit PhysicsLevel(const Level& level);

    size_t GetNodeIndex(const LevelNode* node) const;

    void ResolveImpact(const ImpactRecord& impact);

    void ResolveContactVelocities(const std::span<ImpactRecord>& contacts);

    float ComputeMaxClosingSpeed(const std::span<ImpactRecord>& contacts) const;

    void ResolveContactPenetrations(const std::span<ImpactRecord>& contacts);

    void FindAndResolveAllImpacts();

    [[nodiscard]] bool SphereSphereSweep(const BodyPair& bodies, ImpactResult& impactResult) const;

    // Sorted mapping of nodes to their index in the various arrays below.
    std::vector<NodeAndIndex> m_NodeIndexMap;
    // Nodes in the level that have rigid bodies.
    // These are stored in the same order as the other arrays below.
    std::vector<const LevelNode*> m_Nodes;
    std::vector<float> m_PosPool[2][3];
    std::vector<float> m_LinearVelocitiesPool[3];
    std::vector<float> m_AccelerationPool[2][3];
    std::vector<float> m_Radii; // Radii of the bounding spheres of the rigid bodies.
    std::vector<float> m_InvMasses; // Inverse masses of the rigid bodies.
    // Tracks which bodies are active in the current frame.
    std::vector<bool> m_ActiveBodies;

    //Positions for the current frame.
    VVec3 m_P0;
    //Predicted positions for the next frame.
    VVec3 m_P1;
    //Accelerations for the current frame.
    VVec3 m_A0;
    //Accelerations for the next frame.
    VVec3 m_A1;

    VVec3 m_LinearVelocities;

    GridHash m_GridHash{GRID_CELL_SIZE};

    std::vector<ImpactRecord> m_ImpactRecords;
    std::vector<ImpactRecord> m_ContactRecords;
};

#endif // PL_V1