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

static size_t
CalculateTotalStringSize(std::span<const LevelNodeDef> nodeDefs)
{
    size_t totalSize = 0;
    for(const auto& nodeDef : nodeDefs)
    {
        totalSize += nodeDef.Name.size() + 1; // +1 for null terminator
        totalSize += CalculateTotalStringSize(nodeDef.Children);
    }

    return totalSize;
}

// Collect nodes in breadth-first order.
// Parents come before children, siblings are contiguous.
static Result<>
CollectNodes(std::span<const LevelNodeDef> nodeDefs,
    const PropKit& propKit,
    std::vector<Level::Node>& nodes,
    std::vector<char>& stringStorage)
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

        stringStorage.insert(stringStorage.end(), nodeDef.Name.begin(), nodeDef.Name.end());
        stringStorage.push_back('\0'); // Null terminator for the string_view

        Level::Node node //
            {
                .LocalTransform{ nodeDef.Transform },
                .ModelIndex{ modelIndex },
                .ChildCount{ narrow_cast<uint32_t>(nodeDef.Children.size()) },
            };

        nodes.emplace_back(std::move(node));
    }

    // Now add child nodes.
    for(size_t i = 0; i < nodeDefs.size(); ++i)
    {
        const LevelNodeDef& nodeDef = nodeDefs[i];

        if(nodeDef.Children.empty())
        {
            continue;
        }

        MLG_CHECK(CollectNodes(nodeDef.Children, propKit, nodes, stringStorage));
    }

    return Result<>::Ok;
}

Result<>
Level::Create(const LevelDef& levelDef, const PropKit& propKit, Level& outLevel)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);
    const size_t totalStringSize = CalculateTotalStringSize(levelDef.NodeDefs);

    std::vector<Node> nodes;
    std::vector<char> stringStorage;
    nodes.reserve(nodeCount);
    stringStorage.reserve(totalStringSize);

    // Flatten nodes into breadth-first order.
    MLG_CHECK(CollectNodes(levelDef.NodeDefs, propKit, nodes, stringStorage));

    // Fill in the Name, ParentIndex, FirstChildIndex fields.
    // The child of the first node comes directly after all the nodes in the first level.
    size_t firstChildIndex = levelDef.NodeDefs.size();
    size_t stringOffset = 0;
    for(size_t i = 0; i < nodes.size(); ++i)
    {
        Node& node = nodes[i];
        node.Name = std::string_view(&stringStorage[stringOffset]);
        stringOffset += node.Name.size() + 1; // +1 for null terminator

        if(node.ChildCount == 0)
        {
            continue;
        }

        node.FirstChildIndex = LevelNodeIndex(firstChildIndex);

        for(size_t j = 0; j < node.ChildCount; ++j)
        {
            const size_t childIndex = firstChildIndex + j;
            MLG_ASSERT(childIndex < nodes.size(), "Invalid child index calculated for node {}", node.Name);

            Node& childNode = nodes[childIndex];
            childNode.ParentIndex = LevelNodeIndex(i);
        }

        // The first child of the next node with children comes after all the children of the
        // current node.
        firstChildIndex += node.ChildCount;
    }

    Level level(&propKit, std::move(nodes), std::move(stringStorage));

    outLevel = std::move(level);

    return Result<>::Ok;
}

Level::Level(const PropKit* propKit, std::vector<Node>&& nodes, std::vector<char>&& stringStorage)
    : m_PropKit(propKit),
      m_Nodes(std::move(nodes)),
      m_StringStorage(std::move(stringStorage))
{
    m_RootNodeCount = 0;
    for(const auto& node : m_Nodes)
    {
        // Nodes are stored in breadth-first order, so all root nodes will be at the beginning
        // of the vector.
        if(node.ParentIndex.IsValid())
        {
            break;
        }

        ++m_RootNodeCount;
    }

    // Populate the node handles array and calculate world transforms.

    m_NodeHandles.reserve(m_Nodes.size());

    for(auto& node : m_Nodes)
    {
        m_NodeHandles.emplace_back(NodeHandle(&node));

        if(node.ParentIndex.IsValid())
        {
            const Mat44f& parentWorldXform = m_Nodes[node.ParentIndex.Value()].WorldTransform;
            node.WorldTransform = parentWorldXform * node.LocalTransform.ToMatrix();
        }
        else
        {
            node.WorldTransform = node.LocalTransform.ToMatrix();
        }
    }
}

std::span<const Level::NodeHandle>
Level::GetRoots() const
{
    return std::span<const NodeHandle>(m_NodeHandles).subspan(0, m_RootNodeCount);
}

Result<std::span<const Level::NodeHandle>>
Level::GetChildren(const NodeHandle& handle) const
{
    MLG_CHECKV(handle, "Invalid node handle");
    MLG_CHECKV(IsInLevel(handle), "Node handle points outside of node array");

    if(!handle.m_Node->FirstChildIndex.IsValid())
    {
        return std::span<const NodeHandle>{};
    }

    MLG_CHECKV(handle.m_Node->FirstChildIndex.Value() < m_Nodes.size(), "Invalid FirstChildIndex in node");
    MLG_CHECKV(handle.m_Node->FirstChildIndex.Value() + handle.m_Node->ChildCount <= m_Nodes.size(),
        "Invalid ChildCount in node");

    return std::span<const NodeHandle>(m_NodeHandles).subspan(handle.m_Node->FirstChildIndex.Value(),
        handle.m_Node->ChildCount);
}

Result<Level::NodeHandle>
Level::GetNodeHandle(std::initializer_list<std::string_view> path) const
{
    return GetNodeHandle(std::span<const std::string_view>{ path });
}

Result<const Level::Node*>
Level::GetNode(const NodeHandle& handle) const
{
    MLG_CHECKV(IsInLevel(handle), "Node handle points outside of node array");
    return handle.m_Node;
}

// private:

bool
Level::IsInLevel(const NodeHandle& handle) const
{
    return handle.m_Node >= m_Nodes.data() && handle.m_Node < m_Nodes.data() + m_Nodes.size();
}