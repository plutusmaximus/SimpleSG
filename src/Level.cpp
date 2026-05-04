#include "Level.h"

#include "narrow_cast.h"

static size_t
CountNodes(std::span<const LevelNodeDef> nodeDefs)
{
    size_t count = nodeDefs.size();
    for(const auto& nodeDef : nodeDefs)
    {
        count += CountNodes(nodeDef.Children);
    }

    return count;
}

// Collect nodes in breadth-first order.
// Parents come before children, siblings are contiguous.
static Result<>
CollectNodes(
    std::span<const LevelNodeDef> nodeDefs, const PropKit& propKit, std::vector<LevelNode>& nodes)
{
    // First add nodes from the current level.
    for(const auto& nodeDef : nodeDefs)
    {
        ModelIndex modelIndex = ModelIndex::INVALID;
        if(!nodeDef.ModelName.empty())
        {
            auto result = propKit.GetModelIndex(nodeDef.ModelName);
            MLG_CHECK(result);

            modelIndex = *result;
        }

        LevelNode levelNode //
            {
                .Name{ nodeDef.Name },
                .Transform{ nodeDef.Transform },
                .ModelIndex{ modelIndex },
                .ChildCount{ narrow_cast<uint32_t>(nodeDef.Children.size()) },
            };

        nodes.emplace_back(std::move(levelNode));
    }

    // Now add child nodes.
    for(size_t i = 0; i < nodeDefs.size(); ++i)
    {
        const LevelNodeDef& nodeDef = nodeDefs[i];

        if(nodeDef.Children.empty())
        {
            continue;
        }

        MLG_CHECK(CollectNodes(nodeDef.Children, propKit, nodes));
    }

    return Result<>::Ok;
}

Result<>
Level::Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);

    std::vector<LevelNode> nodes;
    nodes.reserve(nodeCount);

    // Flatten nodes into breadth-first order.
    MLG_CHECK(CollectNodes(levelDef.NodeDefs, propKit, nodes));

    // Fill in the ParentIndex and FirstChildIndex fields.
    // The child of the first node comes directly after all the nodes in the first level.
    size_t firstChildIndex = levelDef.NodeDefs.size();
    for(size_t i = 0; i < nodes.size(); ++i)
    {
        LevelNode& node = nodes[i];
        if(node.ChildCount == 0)
        {
            continue;
        }

        node.FirstChildIndex = LevelNodeIndex(firstChildIndex);

        for(size_t j = 0; j < node.ChildCount; ++j)
        {
            const size_t childIndex = firstChildIndex + j;
            MLG_ASSERT(childIndex < nodes.size(), "Invalid child index calculated for node {}", node.Name);

            LevelNode& childNode = nodes[childIndex];
            childNode.ParentIndex = LevelNodeIndex(i);
        }

        // The first child of the next node with children comes after all the children of the
        // current node.
        firstChildIndex += node.ChildCount;
    }

    Level level(&propKit, std::move(nodes));

    outLevel = std::move(level);

    return Result<>::Ok;
}

Level::Level(const PropKit* propKit, std::vector<LevelNode>&& nodes)
    : m_PropKit(propKit),
      m_Nodes(std::move(nodes))
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

Result<std::span<const LevelNode>>
Level::GetChildNodes(const LevelNode& node) const
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
