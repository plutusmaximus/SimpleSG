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
CollectNodes(std::span<const LevelNodeDef> nodeDefs,
    const PropKit& propKit,
    const LevelNodeIndex parentIndex,
    std::vector<LevelNode>& nodes)
{
    const size_t firstNodeIndex = nodes.size();

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
                .Transform{ nodeDef.Transform },
                .ModelIndex{ modelIndex },
                .ParentIndex{ parentIndex },
                .FirstChildIndex{ LevelNodeIndex::INVALID },
                .ChildCount{ 0 },
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

        const size_t firstChildIndex = nodes.size();

        const size_t thisNodeIndex = firstNodeIndex + i;
        const LevelNodeIndex thisNodeLevelIndex = LevelNodeIndex(thisNodeIndex);

        MLG_CHECK(CollectNodes(nodeDef.Children, propKit, thisNodeLevelIndex, nodes));

        nodes[thisNodeIndex].FirstChildIndex = LevelNodeIndex(firstChildIndex);
        nodes[thisNodeIndex].ChildCount = narrow_cast<uint32_t>(nodeDef.Children.size());
    }

    return Result<>::Ok;
}

Result<>
Level::Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);

    std::vector<LevelNode> nodes;
    std::unordered_map<std::string, size_t> nodeNameToIndex;
    nodes.reserve(nodeCount);
    nodeNameToIndex.reserve(levelDef.NodeDefs.size());

    MLG_CHECK(CollectNodes(levelDef.NodeDefs, propKit, LevelNodeIndex::INVALID, nodes));

    Level level(&propKit, std::move(nodes), std::move(nodeNameToIndex));

    outLevel = std::move(level);

    return Result<>::Ok;
}
