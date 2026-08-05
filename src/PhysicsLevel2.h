#pragma once

#include "GridHash.h"
#include "Level.h"
#include "Result.h"
#include "VecMath.h"

#include <box3d/Box3D.h>

class PhysicsLevel2
{
public:

    static Result<PhysicsLevel2> Create(const Level& level);

    PhysicsLevel2() = default;
    ~PhysicsLevel2() = default;
    PhysicsLevel2(const PhysicsLevel2&) = delete;
    PhysicsLevel2& operator=(const PhysicsLevel2&) = delete;
    PhysicsLevel2(PhysicsLevel2&& other) = default;
    PhysicsLevel2& operator=(PhysicsLevel2&& other) = default;

    void PredictPositions(const float dt);

    void Resolve();

    void ApplyImpulse(const Level::Node* node, const Vec3f& impulse);

    void AddForce(const Level::Node* node, const Vec3f& force);

    void UpdateVelocities(const float dt);

    Result<> SyncToLevel(Level& level);

    size_t GetNodeCount() const { return m_Nodes.size(); }

    std::span<const Level::Node* const> GetNodes() const { return m_Nodes; }
    Vec3f GetPosition(const Level::Node* node) const;
    Vec3f GetLinearVelocity(const Level::Node* node) const;
    float GetRadius(const Level::Node* node) const;
    float GetInverseMass(const Level::Node* node) const;
    void GetPositions(VVec3& positions) const;
    void GetLinearVelocities(VVec3& linearVelocities) const;
    void GetInverseMasses(std::span<float>& invMasses) const;

    void SetLinearVelocity(const Level::Node* node, const Vec3f& velocity);

    // Radians per second
    void SetAngularVelocity(const Level::Node* node, const Vec3f& angularVelocity);

private:

    /// Mapping between a node and its index into the various arrays below.
    /// We maintain a sorted vector of NodeAndIndex so we can quickly find
    /// the index of a node using binary search.
    class NodeAndIndex
    {
    public:
        static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
        
        NodeAndIndex() = delete;

        NodeAndIndex(const Level::Node* node, size_t index)
            : m_Node(node), m_Index(index)
        {
        }

        const Level::Node* GetNode() const { return m_Node; }
        size_t GetIndex() const { return m_Index; }

        friend auto operator<=>(const NodeAndIndex& a, const NodeAndIndex& b)
        {
            return a.m_Node <=> b.m_Node;
        }

        friend bool operator==(const NodeAndIndex& lhs, const NodeAndIndex& rhs) = default;
        friend bool operator!=(const NodeAndIndex& lhs, const NodeAndIndex& rhs) = default;

    private:
        friend class PhysicsLevel2;

        const Level::Node* m_Node{nullptr};
        size_t m_Index{0};
    };

    PhysicsLevel2(std::vector<NodeAndIndex> nodeIndexMap,
                  std::vector<const Level::Node*> nodes,
                  b3WorldId worldId,
                  std::vector<b3ShapeId> shapeIds,
                  std::vector<b3BodyId> bodyIds)
        : m_NodeIndexMap(std::move(nodeIndexMap)),
          m_Nodes(std::move(nodes)),
          m_WorldId(worldId),
          m_ShapeIds(std::move(shapeIds)),
          m_BodyIds(std::move(bodyIds))
    {
    }

    size_t GetNodeIndex(const Level::Node* node) const;

    // Sorted mapping of nodes to their index in the various arrays below.
    std::vector<NodeAndIndex> m_NodeIndexMap;
    // Nodes in the level that have rigid bodies.
    // These are stored in the same order as the other arrays below.
    std::vector<const Level::Node*> m_Nodes;

    [[maybe_unused]] b3WorldId m_WorldId{b3_nullWorldId};
    std::vector<b3ShapeId> m_ShapeIds;
    std::vector<b3BodyId> m_BodyIds;
};