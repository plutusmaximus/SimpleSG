#pragma once

#include "PropKit.h"
#include "Result.h"
#include "SemanticInteger.h"
#include "VecMath.h"

#include <string>
#include <span>
#include <unordered_map>
#include <vector>

struct LevelNodeIndexTag {};
using LevelNodeIndex = SemanticInteger<LevelNodeIndexTag>;

struct LevelNode
{
    TrsTransformf Transform;
    ModelIndex ModelIndex{ ModelIndex::INVALID };
    LevelNodeIndex ParentIndex{ LevelNodeIndex::INVALID };
    LevelNodeIndex FirstChildIndex{ LevelNodeIndex::INVALID };
    uint32_t ChildCount{ 0 };
};

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

    std::span<const LevelNode> GetRootNodes() const { return m_RootNodes; }

    Result<std::span<const LevelNode>> GetChildNodes(const LevelNode& node) const
    {
        if(!node.FirstChildIndex.IsValid())
        {
            return std::span<const LevelNode>{};
        }

        MLG_CHECKV(node.FirstChildIndex.Value() < m_Nodes.size(), "Invalid FirstChildIndex in node");
        MLG_CHECKV(node.FirstChildIndex.Value() + node.ChildCount <= m_Nodes.size(),
            "Invalid ChildCount in node");

        return std::span<const LevelNode>(m_Nodes).subspan(node.FirstChildIndex.Value(),
            node.ChildCount);
    }

private:
    Level(const PropKit* propKit,
        std::vector<LevelNode>&& nodes,
        std::unordered_map<std::string, size_t>&& nodeNameToIndex)
        : m_PropKit(propKit),
          m_Nodes(std::move(nodes)),
          m_NodeNameToIndex(std::move(nodeNameToIndex))
    {
        size_t rootNodeCount = 0;
        for(const auto& node : m_Nodes)
        {
            // Nodes are stored in breadth-first order, so all root nodes will be at the beginning
            // of the vector.
            if(node.ParentIndex.IsValid())
            {
                break;
            }

            ++rootNodeCount;
        }

        m_RootNodes = std::span<const LevelNode>(m_Nodes).subspan(0, rootNodeCount);
    }

    const PropKit* m_PropKit = nullptr;
    std::vector<LevelNode> m_Nodes;
    std::span<const LevelNode> m_RootNodes;
    std::unordered_map<std::string, size_t> m_NodeNameToIndex;
};