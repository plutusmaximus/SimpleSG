#include "Level.h"

#include "PropKit.h"

#include <ranges>

namespace
{
size_t
CountNodes(std::span<const LevelNodeDef> nodeDefs)
{
    size_t count = nodeDefs.size();
    for(const auto& nodeDef : nodeDefs)
    {
        count += CountNodes(nodeDef.Children);
    }

    return count;
}

size_t
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
Result<>
CollectNodes(std::span<const LevelNodeDef> nodeDefs,
    const PropKit& propKit,
    std::vector<Level::Node>& nodes,
    std::vector<char>& stringStorage)
{
    // First add nodes from the current level.
    for(const auto& nodeDef : nodeDefs)
    {
        Level::Components components;

        if(nodeDef.Components.Model)
        {
            const ModelRef& modelRef = *nodeDef.Components.Model;

            MLG_CHECKV(!modelRef.Name.empty(), "ModelRef in node {} is empty", nodeDef.Name);

            auto result = propKit.GetModelId(modelRef.Name);
            MLG_CHECK(result);

            components.Model = *result;
        }

        if(nodeDef.Components.Body)
        {
            const RigidBodyDef& bodyDef = *nodeDef.Components.Body;

            MLG_CHECKV(bodyDef.Mass > 0,
                "RigidBodyDef in node {} has non-positive mass",
                nodeDef.Name);

            components.Body = RigidBody(bodyDef.Mass);
        }

        if(nodeDef.Components.Collider)
        {
            const ColliderDef& colliderDef = *nodeDef.Components.Collider;
            struct Visitor
            {
                Collider operator()(const SphereDef& def) const
                {
                    return Collider{ BoundingSphere{ def.Center, def.Radius } };
                }

                Collider operator()(const BoxDef& def) const
                {
                    const Vec3f p0 = def.Center - def.HalfExtents;
                    const Vec3f p1 = def.Center + def.HalfExtents;
                    return Collider{ BoundingBox{ p0, p1 } };
                }

                Collider operator()(const CapsuleDef& def) const
                {
                    return Collider{ BoundingCapsule{ def.Center, def.Radius, def.HalfHeight } };
                }
            };

            components.Collider = std::visit(Visitor{}, colliderDef);
        }

        stringStorage.insert(stringStorage.end(), nodeDef.Name.begin(), nodeDef.Name.end());
        stringStorage.push_back('\0'); // Null terminator for the string_view

        const Level::Node node //
            {
                .Name{}, // Will be filled in later from stringStorage
                .LocalTransform{ nodeDef.Transform },
                .Components{ components },
                .ChildCount = nodeDef.Children.size(),
            };

        nodes.emplace_back(node);
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
} // namespace

Result<Level>
Level::Create(const LevelDef& levelDef, const PropKit& propKit)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);
    const size_t totalStringSize = CalculateTotalStringSize(levelDef.NodeDefs);

    std::vector<Node> nodes;
    std::vector<char> stringStorage;
    nodes.reserve(nodeCount);
    stringStorage.reserve(totalStringSize);

    // Flatten nodes into breadth-first order.
    MLG_CHECK(CollectNodes(levelDef.NodeDefs, propKit, nodes, stringStorage));

    const std::span<Node> nodesSpan(nodes);

    // Fill in the Name, Parent, FirstChild fields.
    // The children of the first node come directly after all the nodes in the same
    // level as the parent.  The children of the next node with children come after that.
    // E.g.
    // N0,N1,N2,CN0_0,CN0_1,CN1_0,CN1_1,CN2_0
    // where N = node, CN = child node, and the number after the underscore is the index of the
    // child within its siblings.
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

        MLG_ASSERT(firstChildIndex < nodes.size(),
            "Invalid first child index calculated for node {}",
            node.Name);
        MLG_ASSERT(firstChildIndex + node.ChildCount <= nodes.size(),
            "Not enough nodes to assign as children for node {}",
            node.Name);

        node.FirstChild = NodeHandle(firstChildIndex);

        const std::span<Node> childNodes = nodesSpan.subspan(firstChildIndex, node.ChildCount);

        for(Node& childNode : childNodes)
        {
            childNode.Parent = NodeHandle(i);
        }

        // The first child of the next node with children comes after all the children of the
        // current node.
        firstChildIndex += node.ChildCount;
    }

    Level level(std::move(nodes), std::move(stringStorage));

    return std::move(level);
}

Level::Level(std::vector<Node>&& nodes, std::vector<char>&& stringStorage)
    : m_Nodes(std::move(nodes)),
      m_StringStorage(std::move(stringStorage))
{
    size_t rootNodeCount = 0;

    // Count root nodes.
    // Nodes are stored in breadth-first order, so all root nodes will be at the beginning
    // of the vector.
    for(const auto& node : m_Nodes)
    {
        if(node.Parent)
        {
            // No more root nodes after this.
            break;
        }

        ++rootNodeCount;
    }

    // Populate the node handles array and calculate world transforms.

    m_NodeHandles.reserve(m_Nodes.size());

    for(size_t i = 0; i < m_Nodes.size(); ++i)
    {
        m_NodeHandles.emplace_back(NodeHandle(i));
    }

    // Calculate world transforms for all nodes.
    for(auto& node : m_Nodes)
    {
        if(node.Parent)
        {
            const Node* parent = GetNode(node.Parent);
            node.WorldTransform = parent->WorldTransform * node.LocalTransform.ToMatrix();
        }
        else
        {
            node.WorldTransform = node.LocalTransform.ToMatrix();
        }
    }

    m_RootNodes = std::span(m_NodeHandles).subspan(0, rootNodeCount);
}

Result<std::span<const Level::NodeHandle>>
Level::GetChildren(const NodeHandle& handle) const
{
    const Node* node = GetNode(handle);
    MLG_CHECK(node);

    if(!node->FirstChild)
    {
        return std::span<const NodeHandle>{};
    }

    MLG_CHECKV(IsInLevel(node->FirstChild), "Invalid FirstChild handle in node {}", node->Name);
    MLG_CHECKV(node->FirstChild.GetValue() + node->ChildCount <= m_Nodes.size(),
        "Invalid ChildCount in node {}", node->Name);

    return std::span(m_NodeHandles).subspan(node->FirstChild.GetValue(), node->ChildCount);
}

Result<Level::NodeHandle>
Level::GetNodeHandle(std::initializer_list<std::string_view> path) const
{
    return GetNodeHandle(std::span{ path });
}

const Level::Node*
Level::GetNode(const NodeHandle& handle) const
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return nullptr;
    }

    return &m_Nodes[handle.GetValue()];
}

Result<>
Level::UpdateLocalTransform(const NodeHandle& handle, const TrsTransformf& localTransform)
{
    Node* node = GetNode(handle);
    MLG_CHECKV(node);

    node->LocalTransform = localTransform;

    const NodeHandle handles[] = { handle };

    // Update the world transform of the node and all its descendants.
    // TODO(KB) - this defer updating of world transforms.
    UpdateWorldTransforms(handles);

    return Result<>::Ok;
}

void
Level::SetActive(const NodeHandle& handle, bool active)
{
    Node* node = GetNode(handle);

    if(!MLG_VERIFY(node))
    {
        return;
    }

    node->Flags = active ? (node->Flags | NodeFlags::Active) : (node->Flags & ~NodeFlags::Active);

    auto children = GetChildren(handle);
    if(children)
    {
        for(const NodeHandle& childHandle : *children)
        {
            SetActive(childHandle, active);
        }
    }
}

bool
Level::IsActive(const NodeHandle& handle) const
{
    const Node* node = GetNode(handle);

    return MLG_VERIFY(node) && (node->Flags & NodeFlags::Active) == NodeFlags::Active;
}

void
Level::SetVisible(const NodeHandle& handle, bool visible)
{
    Node* node = GetNode(handle);

    if(!MLG_VERIFY(node))
    {
        return;
    }

    node->Flags = visible ? (node->Flags | NodeFlags::Visible) : (node->Flags & ~NodeFlags::Visible);

    auto children = GetChildren(handle);
    if(children)
    {
        for(const NodeHandle& childHandle : *children)
        {
            SetVisible(childHandle, visible);
        }
    }
}

bool
Level::IsVisible(const NodeHandle& handle) const
{
    const Node* node = GetNode(handle);

    return MLG_VERIFY(node) && (node->Flags & NodeFlags::Visible) == NodeFlags::Visible;
}

// private:

bool
Level::IsInLevel(const NodeHandle& handle) const
{
    return handle.GetValue() < m_Nodes.size();
}

Level::Node*
Level::GetNode(const NodeHandle& handle)
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return nullptr;
    }

    return &m_Nodes[handle.GetValue()];
}

void
Level::UpdateWorldTransforms(std::span<const NodeHandle> nodes)
{
    for (const NodeHandle nodeHandle : nodes)
    {
        Node* node = GetNode(nodeHandle);
        if(!MLG_VERIFY(node, "Invalid node handle found while updating world transforms"))
        {
            continue;
        }
        
        if(node->Parent)
        {
            const Node* parent = GetNode(node->Parent);
            node->WorldTransform = parent->WorldTransform * node->LocalTransform.ToMatrix();
        }
        else
        {
            // No parent - the world transform is the same as the local transform.
            node->WorldTransform = node->LocalTransform.ToMatrix();
        }

        auto childNodes = GetChildren(nodeHandle);
        if(!MLG_VERIFY(childNodes, "Failed to get children of node while updating world transforms"))
        {
            continue;
        }

        if(!childNodes->empty())
        {
            UpdateWorldTransforms(*childNodes);
        }
    }
}