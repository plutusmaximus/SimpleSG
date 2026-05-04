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

struct LevelNodeDef
{
    std::string Name;
    TrsTransformf Transform;
    std::string ModelName;
    std::vector<LevelNodeDef> Children;
};

struct LevelDef
{
    std::vector<LevelNodeDef> NodeDefs;
};

struct LevelNode
{
    std::string_view Name;
    TrsTransformf Transform;
    ModelIndex ModelIndex{ ModelIndex::INVALID };
    LevelNodeIndex ParentIndex{ LevelNodeIndex::INVALID };
    LevelNodeIndex FirstChildIndex{ LevelNodeIndex::INVALID };
    uint32_t ChildCount{ 0 };
};

class Level
{
public:
    static Result<> Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel);

    Level() = default;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) = default;
    Level& operator=(Level&& other) = default;

    // Retuns a span of all nodes in the level, in breadth-first order.
    std::span<const LevelNode> GetAllNodes() const { return m_Nodes; }

    std::span<const LevelNode> GetRootNodes() const
    {
        return std::span<const LevelNode>(m_Nodes).subspan(0, m_RootNodeCount);
    }

    Result<std::span<const LevelNode>> GetChildNodes(const LevelNode& node) const;

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
    Result<const LevelNode*> GetNode(R&& path) const
    {
        const LevelNode* foundNode = nullptr;

        const auto pathLen = std::ranges::size(path);
        size_t pathIndex = 0;

        std::span<const LevelNode> nodesToSearch = GetRootNodes();
        for (auto&& x : path)
        {
            std::string_view part = x;

            const LevelNode* node = nullptr;

            for(const auto & tmpNode : nodesToSearch)
            {
                if(tmpNode.Name == part)
                {
                    node = &tmpNode;
                    break;
                }
            }

            MLG_CHECKV(node != nullptr, "Node not found: {}", part);

            if(pathIndex == pathLen - 1)
            {
                foundNode = node;
                break;
            }

            ++pathIndex;

            auto result = GetChildNodes(*node);
            MLG_CHECK(result);
            nodesToSearch = *result;
        }

        MLG_CHECKV(foundNode != nullptr, "Node not found: {}", path);

        return foundNode;
    }

    // Fetches a node by its path from the root, e.g. {"RootNode", "ChildNode", "GrandchildNode"}.
    // This overload is provided for convenience to allow passing an initializer list directly
    // without having to wrap it in a std::span or other container.
    // - GetNode({"RootNode", "ChildNode", "GrandchildNode"});
    Result<const LevelNode*> GetNode(std::initializer_list<std::string_view> path) const
    {
        return GetNode(std::span<const std::string_view>{path});
    }

private:
    Level(
        const PropKit* propKit, std::vector<LevelNode>&& nodes, std::vector<char>&& stringStorage);

    const PropKit* m_PropKit{ nullptr };
    std::vector<LevelNode> m_Nodes;
    size_t m_RootNodeCount{ 0 };
    // Storage for node names to ensure they remain valid for string_views
    // and to reduce memory fragmentation by storing all names contiguously.
    std::vector<char> m_StringStorage;
};