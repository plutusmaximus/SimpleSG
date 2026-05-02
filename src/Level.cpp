#include "Level.h"

#include "PropKit.h"

static Result<size_t>
CountNodes(const LevelDef& levelDef, const PropKit& propKit)
{
    size_t count = 0;
    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECK(assembly);

        count += (*assembly)->Nodes.size();
    }

    return count;
}

// Collect nodes in breadth-first order.
// Parents come before children, siblings are contiguous.
static Result<>
CollectNodes(const Assembly& assembly,
    const std::span<const AssemblyNode>& assemblyNodes,
    const PropKit& propKit,
    std::vector<LevelNode>& nodes)
{
    // Add all sibling nodes contiguously.
    for(const auto& node : assemblyNodes)
    {
        LevelNode levelNode //
            {
                .Transform = node.Transform,
            };

        nodes.emplace_back(std::move(levelNode));
    }

    size_t nodeIndex = nodes.size() - assemblyNodes.size();

    // For each sibling node recursively add its children.
    for(size_t i = 0; i < assemblyNodes.size(); ++i, ++nodeIndex)
    {
        const AssemblyNode& asmNode = assemblyNodes[i];

        if(asmNode.ChildCount == 0)
        {
            nodes[nodeIndex].FirstChildIndex = LevelNodeIndex::INVALID;
            continue;
        }

        nodes[nodeIndex].FirstChildIndex = LevelNodeIndex(nodes.size());

        auto children = assembly.GetChildren(asmNode);
        MLG_CHECK(children);

        MLG_CHECK(CollectNodes(assembly, *children, propKit, nodes));
    }

    return Result<>::Ok;
}

static Result<>
CollectNodes(const LevelDef& levelDef,
    const PropKit& propKit,
    std::vector<LevelNode>& nodes,
    std::unordered_map<std::string, size_t>& nodeNameToIndex)
{
    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        MLG_CHECKV(!nodeNameToIndex.contains(nodeDef.Name),
            "Duplicate node name in level definition: {}",
            nodeDef.Name);

        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECK(assembly);

        std::array<AssemblyNode, 1> rootNodes{(*assembly)->Nodes[0]};

        const size_t currentIndex = nodes.size();

        MLG_CHECK(CollectNodes(**assembly, rootNodes, propKit, nodes));

        nodeNameToIndex[nodeDef.Name] = currentIndex;
    }

    return Result<>::Ok;
}

Result<>
Level::Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel)
{
    auto nodeCount = CountNodes(levelDef, propKit);
    MLG_CHECK(nodeCount);

    std::vector<LevelNode> nodes;
    std::unordered_map<std::string, size_t> nodeNameToIndex;
    nodes.reserve(*nodeCount);
    nodeNameToIndex.reserve(levelDef.NodeDefs.size());

    MLG_CHECK(CollectNodes(levelDef, propKit, nodes, nodeNameToIndex));

    Level level(&propKit, std::move(nodes), std::move(nodeNameToIndex));

    outLevel = std::move(level);

    return Result<>::Ok;
}
