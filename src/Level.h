#pragma once

#include "LevelDefs.h"
#include "LevelTypes.h"
#include "Result.h"

#include <span>
#include <string_view>
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

    /// @brief Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode",
    /// "GrandchildNode"}. the path argument can take the following forms:
    /// const char* nodePath[] {"RootNode", "ChildNode", "GrandchildNode"}; GetNode(nodePath);
    /// std::array<const char*, 3> nodePath{"RootNode", "ChildNode", "GrandchildNode"};
    /// GetNode(nodePath);
    ///
    /// const std::string nodePath[] { "RootNode", "ChildNode", "GrandchildNode" };
    /// GetNode(nodePath);
    ///
    /// std::array<std::string, 3> nodePath{ "RootNode", "ChildNode", "GrandchildNode" };
    /// GetNode(nodePath);
    ///
    /// std::vector<std::string> nodePath{ "RootNode", "ChildNode", "GrandchildNode" };
    /// GetNode(nodePath);
    ///
    /// std::vector<const char*> nodePath{ "RootNode", "ChildNode", "GrandchildNode" };
    /// GetNode(nodePath);
    ///
    /// and any other contiguous range of strings or string views that can be converted to
    /// std::string_view
    template<std::ranges::sized_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
    const LevelNode* GetNode(const R& path) const
    {
        return GetNode(std::span{ path });
    }

    // Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    // This overload is provided for convenience to allow passing an initializer list directly
    // without having to wrap it in a std::span or other container.
    // - GetNode({"RootNode", "ChildNode", "GrandchildNode"});
    const LevelNode* GetNode(std::initializer_list<std::string_view> path) const;

    const LevelNode* GetNode(const std::span<const std::string_view> path) const;

    void Update(const float timeStep);

    void SetActive(const LevelNode& node, bool active);

    void SetVisible(const LevelNode& node, bool visible);

private:
    Level(std::vector<LevelNode>&& nodes,
        std::vector<PhysicsNode>&& physicsNodes,
        std::vector<ModelNode>&& modelNodes,
        const WorldIdentifier worldId,
        StringArena&& stringArena);

    template<typename T>
    static Result<> CollectNodes(T nodeDefs,
        const PropKit& propKit,
        const WorldIdentifier worldId,
        const LevelNode* parentNode,
        std::vector<LevelNode>& nodes,
        std::vector<PhysicsNode>& physicsNodes,
        std::vector<ModelNode>& modelNodes,
        StringArena& stringArena);

    LevelNode* GetNode(const LevelNode& node);

    // Returns true if the node is in the level.
    bool IsInLevel(const LevelNode& node) const;

    void UpdateWorldTransforms(std::span<LevelNode> nodes);

    std::vector<LevelNode> m_Nodes;
    std::vector<PhysicsNode> m_PhysicsNodes;
    std::vector<ModelNode> m_ModelNodes;
    std::span<LevelNode> m_RootNodes;
    StringArena m_StringArena;
    WorldIdentifier m_WorldId;
};