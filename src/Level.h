#pragma once

#include "LevelDefs.h"
#include "LevelTypes.h"
#include "Result.h"

#include <span>
#include <vector>

class PropKit;

class Level
{
public:
    static Result<Level> Create(const LevelDef& levelDef, const PropKit& propKit);

    Level() = delete;
    ~Level();
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) = default;
    Level& operator=(Level&& other) = default;

    /// @brief Returns all nodes in the level, in breadth-first order.
    std::span<const LevelNode> GetAllNodes() const { return m_Nodes; }

    /// @brief Returns all physics nodes in the level, in breadth-first order.
    std::span<const PhysicsNode> GetAllPhysicsNodes() const { return m_PhysicsNodes; }
    std::span<PhysicsNode> GetAllPhysicsNodes() { return m_PhysicsNodes; }

    /// @brief Returns all model nodes in the level, in breadth-first order.
    std::span<const ModelNode> GetAllModelNodes() const { return m_ModelNodes; }
    std::span<ModelNode> GetAllModelNodes() { return m_ModelNodes; }

    /// @brief Returns the root nodes of the level. Root nodes are nodes that have no parent.
    std::span<const LevelNode> GetRoots() const { return m_RootNodes; }

    void Update(const float timeStep);

    void SetActive(const LevelNode& node, bool active);

    void SetVisible(const LevelNode& node, bool visible);

private:
    Level(std::vector<LevelNode>&& nodes,
        std::vector<PhysicsNode>&& physicsNodes,
        std::vector<ModelNode>&& modelNodes,
        const WorldIdentifier worldId);

    template<typename T>
    static Result<> CollectNodes(T nodeDefs,
        const PropKit& propKit,
        const WorldIdentifier worldId,
        const LevelNode* parentNode,
        std::vector<LevelNode>& nodes,
        std::vector<PhysicsNode>& physicsNodes,
        std::vector<ModelNode>& modelNodes);

    LevelNode* GetNode(const LevelNode& node);

    // Returns true if the node is in the level.
    bool IsInLevel(const LevelNode& node) const;

    void UpdateWorldTransforms(std::span<LevelNode> nodes);

    std::vector<LevelNode> m_Nodes;
    std::vector<PhysicsNode> m_PhysicsNodes;
    std::vector<ModelNode> m_ModelNodes;
    std::span<LevelNode> m_RootNodes;
    WorldIdentifier m_WorldId;
};