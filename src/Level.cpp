#include "Level.h"

#include "PropKit.h"

static size_t
CountNodes(const AssemblyNode& node)
{
    size_t count = 1; // Count the current node.

    for(const auto& childNode : node.Children)
    {
        count += CountNodes(childNode);
    }

    return count;
}

static Result<size_t>
CountNodes(const Assembly& assembly, const PropKit& propKit)
{
    auto rootNode = propKit.GetAssemblyNode(assembly.RootNodeIndex);
    MLG_CHECK(rootNode);

    return CountNodes(**rootNode);
}

static Result<size_t>
CountNodes(const LevelDef& levelDef, const PropKit& propKit)
{
    size_t count = 0;
    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECK(assembly);

        auto nodeCount = CountNodes(**assembly, propKit);
        MLG_CHECK(nodeCount);

        count += *nodeCount;
    }

    return count;
}

static Result<>
CollectNodes(const AssemblyNode& node, const PropKit& propKit, std::vector<LevelNode>& nodes)
{
    LevelNode levelNode //
        {
            .Transform = node.Transform,
        };

    const size_t currentIndex = nodes.size();
    nodes.emplace_back(std::move(levelNode));

    for(const auto& childNode : node.Children)
    {
        MLG_CHECK(CollectNodes(childNode, propKit, nodes));
    }

    nodes[currentIndex].Children =
        std::span<LevelNode>{ nodes.data() + currentIndex + 1, node.Children.size() };

    return Result<>::Ok;
}

static Result<>
CollectNodes(const Assembly& assembly, const PropKit& propKit, std::vector<LevelNode>& nodes)
{
    auto rootNode = propKit.GetAssemblyNode(assembly.RootNodeIndex);
    MLG_CHECK(rootNode);

    return CollectNodes(**rootNode, propKit, nodes);
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

        const size_t currentIndex = nodes.size();

        MLG_CHECK(CollectNodes(**assembly, propKit, nodes));

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
