#pragma once

#include "PropKit.h"
#include "Result.h"
#include "SemanticInteger.h"
#include "VecMath.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct LevelNodeIndexTag {};
using LevelNodeIndex = SemanticInteger<LevelNodeIndexTag>;

struct ModelRef final
{
    std::string Name;
};

struct RigidBodyDef final
{
    Vec3f Velocity{ 0 };
    float Mass{ 0 };
};

struct SphereDef final
{
    float Radius{ 0 };
};

struct BoxDef final
{
    Vec3f HalfExtents{ 0 };
};

struct CapsuleDef final
{
    float Radius{ 0 };
    float HalfHeight{ 0 };
};

using ColliderDef = std::variant<std::monostate,
    SphereDef,
    BoxDef,
    CapsuleDef>;

struct ComponentsDef final
{
    std::optional<ModelRef> Model;
    std::optional<RigidBodyDef> Body;
    std::optional<ColliderDef> Collider;
};

struct LevelNodeDef
{
    std::string Name;
    TrsTransformf Transform;
    ComponentsDef Components;
    std::vector<LevelNodeDef> Children;
};

struct LevelDef
{
    std::vector<LevelNodeDef> NodeDefs;
};

class Level
{
public:

    struct Node
    {
        std::string_view Name;
        TrsTransformf LocalTransform;
        Mat44f WorldTransform{ 1 };
        ModelIndex ModelIndex{ ModelIndex::INVALID };
        LevelNodeIndex ParentIndex{ LevelNodeIndex::INVALID };
        LevelNodeIndex FirstChildIndex{ LevelNodeIndex::INVALID };
        uint32_t ChildCount{ 0 };
    };

    class NodeHandle
    {
    public:

        NodeHandle() = default;
        NodeHandle(const NodeHandle&) = default;
        NodeHandle& operator=(const NodeHandle&) = default;
        NodeHandle(NodeHandle&&) = default;
        NodeHandle& operator=(NodeHandle&&) = default;

        bool operator==(const NodeHandle& that) const
        {
            return m_Node == that.m_Node;
        }

        bool operator!=(const NodeHandle& that) const
        {
            return m_Node != that.m_Node;
        }

        // Used for sorting and binary search.
        bool operator<(const NodeHandle& that) const
        {
            return m_Node < that.m_Node;
        }

        operator bool() const
        {
            return m_Node != nullptr;
        }

    private:
        friend class Level;

        explicit NodeHandle(const Node* node)
            : m_Node(node)
        {
        }

        const Node* m_Node{nullptr};
    };

    static Result<> Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel);

    Level() = default;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) = default;
    Level& operator=(Level&& other) = default;

    // Retuns a span of all nodes in the level, in breadth-first order.
    std::span<const NodeHandle> GetAllHandles() const { return m_NodeHandles; }

    std::span<const NodeHandle> GetRoots() const;

    Result<std::span<const NodeHandle>> GetChildren(const NodeHandle& handle) const;

    // Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    // the path argument can take the following forms:
    // - const char* nodePath[] {"RootNode", "ChildNode", "GrandchildNode"}; GetNode(nodePath);
    // - std::array<const char*, 2> nodePath{"RootNode", "ChildNode", "GrandchildNode"}; GetNode(nodePath);
    // - const std::string nodePath[] { "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    // - std::array<std::string, 2> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    // - std::vector<std::string> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    // - std::vector<const char*> nodePath{ "RootNode", "ChildNode", "GrandchildNode" }; GetNode(nodePath);
    // - and any other contiguous range of strings or string views that can be converted to std::string_view
    template <std::ranges::sized_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>
    Result<NodeHandle> GetNodeHandle(R&& path) const
    {
        NodeHandle foundHandle;

        const auto pathLen = std::ranges::size(path);
        size_t pathIndex = 0;

        std::span<const NodeHandle> nodesToSearch = GetRoots();
        for (auto&& x : path)
        {
            std::string_view part = x;

            NodeHandle handle;

            for(const auto & tmpHandle : nodesToSearch)
            {
                if(tmpHandle.m_Node->Name == part)
                {
                    handle = tmpHandle;
                    break;
                }
            }

            MLG_CHECKV(handle, "Node not found: {}", part);

            if(pathIndex == pathLen - 1)
            {
                foundHandle = handle;
                break;
            }

            ++pathIndex;

            auto result = GetChildren(handle);
            MLG_CHECK(result);
            nodesToSearch = *result;
        }

        MLG_CHECKV(foundHandle, "Node not found: {}", path);

        return foundHandle;
    }

    // Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    // This overload is provided for convenience to allow passing an initializer list directly
    // without having to wrap it in a std::span or other container.
    // - GetNode({"RootNode", "ChildNode", "GrandchildNode"});
    Result<NodeHandle> GetNodeHandle(std::initializer_list<std::string_view> path) const;

    Result<const Node*> GetNode(const NodeHandle& handle) const;

    Result<> UpdateLocalTransform(const NodeHandle& handle, const TrsTransformf& localTransform);

private:
    Level(
        const PropKit* propKit, std::vector<Node>&& nodes, std::vector<char>&& stringStorage);

    // Returns true if the node handle refers to a node within the level.
    bool IsInLevel(const NodeHandle& handle) const;

    void UpdateWorldTransforms(std::span<Node> nodes);

    const PropKit* m_PropKit{ nullptr };
    std::vector<Node> m_Nodes;
    std::vector<NodeHandle> m_NodeHandles;
    size_t m_RootNodeCount{ 0 };
    // Storage for node names to ensure they remain valid for string_views
    // and to reduce memory fragmentation by storing all names contiguously.
    std::vector<char> m_StringStorage;
};