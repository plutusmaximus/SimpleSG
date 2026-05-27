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

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

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
        Level::Components components;

        if(nodeDef.Components.Model.has_value())
        {
            const ModelRef& modelRef = *nodeDef.Components.Model;

            MLG_CHECKV(!modelRef.Name.empty(), "ModelRef in node {} is empty", nodeDef.Name);

            auto result = propKit.GetModelIndex(modelRef.Name);
            MLG_CHECK(result);

            components.Model = *result;
        }

        if(nodeDef.Components.Body.has_value())
        {
            const RigidBodyDef& bodyDef = *nodeDef.Components.Body;

            MLG_CHECKV(bodyDef.Mass > 0,
                "RigidBodyDef in node {} has non-positive mass",
                nodeDef.Name);

            components.Body = RigidBody //
                {
                    .LinearVelocity = bodyDef.LinearVelocity,
                    .Mass = bodyDef.Mass,
                };
        }

        if(nodeDef.Components.Collider.has_value())
        {
            const ColliderDef& colliderDef = *nodeDef.Components.Collider;
            const auto visitor = overloads //
                {
                    [](const SphereDef& def) -> Collider
                    { return Collider{ Sphere{ def.Radius } }; },
                    [](const BoxDef& def) -> Collider
                    { return Collider{ Box{ def.HalfExtents } }; },
                    [](const CapsuleDef& def) -> Collider
                    { return Collider{ Capsule{ def.Radius, def.HalfHeight } }; },
                };

            components.Collider = std::visit(visitor, colliderDef);
        }

        stringStorage.insert(stringStorage.end(), nodeDef.Name.begin(), nodeDef.Name.end());
        stringStorage.push_back('\0'); // Null terminator for the string_view

        Level::Node node //
            {
                .Name{}, // Will be filled in later from stringStorage
                .LocalTransform{ nodeDef.Transform },
                .Components{ std::move(components) },
                .ChildCount = narrow_cast<uint32_t>(nodeDef.Children.size()),
            };

        nodes.emplace_back(std::move(node));
    }

    // Now add child nodes.
    for(const LevelNodeDef& nodeDef : nodeDefs)
    {
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

    Level level(std::move(nodes), std::move(stringStorage));

    outLevel = std::move(level);

    return Result<>::Ok;
}

Level::Level(std::vector<Node>&& nodes, std::vector<char>&& stringStorage)
    : m_Nodes(std::move(nodes)),
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
    return std::span(m_NodeHandles).subspan(0, m_RootNodeCount);
}

Result<std::span<const Level::NodeHandle>>
Level::GetChildren(const NodeHandle& handle) const
{
    MLG_CHECKV(IsInLevel(handle), "Node is not in level");

    if(!handle.m_Node->FirstChildIndex.IsValid())
    {
        return std::span<const NodeHandle>{};
    }

    MLG_CHECKV(handle.m_Node->FirstChildIndex.Value() < m_Nodes.size(), "Invalid FirstChildIndex in node");
    MLG_CHECKV(handle.m_Node->FirstChildIndex.Value() + handle.m_Node->ChildCount <= m_Nodes.size(),
        "Invalid ChildCount in node");

    return std::span(m_NodeHandles).subspan(handle.m_Node->FirstChildIndex.Value(),
        handle.m_Node->ChildCount);
}

Result<Level::NodeHandle>
Level::GetNodeHandle(std::initializer_list<std::string_view> path) const
{
    return GetNodeHandle(std::span{ path });
}

Result<const Level::Node*>
Level::GetNode(const NodeHandle& handle) const
{
    MLG_CHECKV(IsInLevel(handle), "Node is not in level");
    return handle.m_Node;
}

Result<>
Level::UpdateLocalTransform(const NodeHandle& handle, const TrsTransformf& localTransform)
{
    MLG_CHECKV(IsInLevel(handle), "Node is not in level");

    Node* node = const_cast<Node*>(handle.m_Node);
    node->LocalTransform = localTransform;
    if(!node->ParentIndex.IsValid())
    {
        node->WorldTransform = localTransform.ToMatrix();
    }

    UpdateWorldTransforms({ node, 1 });

    return Result<>::Ok;
}

Result<Level::NodeFlags>
Level::GetNodeFlags(const NodeHandle& handle) const
{
    MLG_CHECKV(IsInLevel(handle), "Node is not in level");

    return handle.m_Node->Flags;
}

void
Level::SetActive(const NodeHandle& handle, bool active)
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return;
    }

    Node* node = const_cast<Node*>(handle.m_Node);
    node->Flags = active ? (node->Flags | NodeFlags::Active) : (node->Flags & ~NodeFlags::Active);

    auto children = GetChildren(handle);
    if(!children)
    {
        return;
    }

    for(const auto& childHandle : *children)
    {
        SetActive(childHandle, active);
    }
}

bool
Level::IsActive(const NodeHandle& handle) const
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return false;
    }

    return (handle.m_Node->Flags & NodeFlags::Active) == NodeFlags::Active;
}

void
Level::SetVisible(const NodeHandle& handle, bool visible)
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return;
    }

    Node* node = const_cast<Node*>(handle.m_Node);
    node->Flags = visible ? (node->Flags | NodeFlags::Visible) : (node->Flags & ~NodeFlags::Visible);

    auto children = GetChildren(handle);
    if(!children)
    {
        return;
    }

    for(const auto& childHandle : *children)
    {
        SetVisible(childHandle, visible);
    }
}

bool
Level::IsVisible(const NodeHandle& handle) const
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return false;
    }

    return (handle.m_Node->Flags & NodeFlags::Visible) == NodeFlags::Visible;
}

// private:

bool
Level::IsInLevel(const NodeHandle& handle) const
{
    return handle.IsValid() && handle.m_Node >= m_Nodes.data() &&
           handle.m_Node < m_Nodes.data() + m_Nodes.size();
}

void
Level::UpdateWorldTransforms(std::span<Node> nodes)
{
    for (auto& node : nodes)
    {
        if(node.ParentIndex.IsValid())
        {
            const Mat44f& parentWorldXform = m_Nodes[node.ParentIndex.Value()].WorldTransform;
            node.WorldTransform = parentWorldXform * node.LocalTransform.ToMatrix();
        }

        if(node.ChildCount > 0)
        {
            const std::span children =
                std::span(m_Nodes).subspan(node.FirstChildIndex.Value(), node.ChildCount);
            UpdateWorldTransforms(children);
        }
    }
}