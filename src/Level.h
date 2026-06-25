#pragma once

#include "LevelDefs.h"
#include "Result.h"
#include "SceneTypes.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

class PropKit;

class Level
{
public:

    enum class NodeFlags : uint8_t
    {
        None = 0,
        Active = 1 << 0,
        Visible = 1 << 1,
        All = Active | Visible
    };

    friend NodeFlags operator|(const NodeFlags a, const NodeFlags b)
    {
        using U = std::underlying_type_t<Level::NodeFlags>;
        return static_cast<NodeFlags>(static_cast<U>(a) | static_cast<U>(b));
    }

    friend NodeFlags operator&(const NodeFlags a, const NodeFlags b)
    {
        using U = std::underlying_type_t<Level::NodeFlags>;
        return static_cast<NodeFlags>(static_cast<U>(a) & static_cast<U>(b));
    }

    friend NodeFlags operator~(const NodeFlags a)
    {
        using U = std::underlying_type_t<Level::NodeFlags>;

        return static_cast<NodeFlags>(
            static_cast<U>(~static_cast<U>(a)) & static_cast<U>(NodeFlags::All));
    }

    struct Components
    {
        std::optional<const Model*> Model;
        std::optional<RigidBody> Body;
        std::optional<Collider> Collider;
    };

    struct Node
    {
        bool IsActive() const { return (Flags & NodeFlags::Active) == NodeFlags::Active; }
        bool IsVisible() const { return (Flags & NodeFlags::Visible) == NodeFlags::Visible; }

        StringHandle Name;
        TrsTransformf LocalTransform;
        Mat44f WorldTransform{ 1 };
        Components Components;
        const Node* Parent{nullptr};
        std::span<const Node> Children;
        NodeFlags Flags{ NodeFlags::Active | NodeFlags::Visible };
    };

    static Result<Level> Create(const LevelDef& levelDef, const PropKit& propKit);

    Level() = delete;
    ~Level() = default;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) = default;
    Level& operator=(Level&& other) = default;

    /// @brief Returns all nodes in the level, in breadth-first order.
    std::span<const Node> GetAllNodes() const { return m_Nodes; }

    /// @brief Returns the root nodes of the level. Root nodes are nodes that have no parent.
    std::span<const Node> GetRoots() const { return m_RootNodes; }

    /// @brief Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    /// the path argument can take the following forms:
    /// - const char* nodePath[] {"RootNode", "ChildNode", "GrandchildNode"}; GetNode(nodePath);
    /// - std::array<const char*, 3> nodePath{"RootNode", "ChildNode", "GrandchildNode"}; GetNode(nodePath);
    /// - const std::string nodePath[] { "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    /// - std::array<std::string, 3> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    /// - std::vector<std::string> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    /// - std::vector<const char*> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    /// - and any other contiguous range of strings or string views that can be converted to std::string_view
    template <std::ranges::sized_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
    Result<const Node*> GetNode(const R& path) const
    {
        const Node* foundNode{ nullptr };

        const auto pathLen = std::ranges::size(path);
        size_t pathIndex = 0;

        std::span<const Node> nodesToSearch = GetRoots();
        for (auto&& x : path)
        {
            std::string_view part = x;

            const Node* node = nullptr;

            for(const auto & tmpNode : nodesToSearch)
            {
                if(tmpNode.Name == part)
                {
                    node = &tmpNode;
                    break;
                }
            }

            MLG_CHECKV(node, "Node not found: {}", part);

            if(pathIndex == pathLen - 1)
            {
                foundNode = node;
                break;
            }

            ++pathIndex;

            nodesToSearch = node->Children;
        }

        auto formatPath = [](const auto& inPath) -> std::string
        {
            std::string result;
            for (auto&& x : inPath)
            {
                if (!result.empty())
                {
                    result += ".";
                }
                result += x;
            }
            return result;
        };

         MLG_CHECKV(foundNode, "Node not found: {}", formatPath(path));

        return foundNode;
    }

    // Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    // This overload is provided for convenience to allow passing an initializer list directly
    // without having to wrap it in a std::span or other container.
    // - GetNode({"RootNode", "ChildNode", "GrandchildNode"});
    Result<const Node*> GetNode(std::initializer_list<std::string_view> path) const;

    Result<> UpdateLocalTransform(const Node& node, const TrsTransformf& localTransform);

    void SetActive(const Node& node, bool active);

    void SetVisible(const Node& node, bool visible);

private:
    Level(std::vector<Node>&& nodes, StringArena&& stringArena);

    Node* GetNode(const Node& node);

    // Returns true if the node is in the level.
    bool IsInLevel(const Node& node) const;

    void UpdateWorldTransforms(std::span<const Node> nodes);

    std::vector<Node> m_Nodes;
    std::span<Node> m_RootNodes;
    StringArena m_StringArena;
};