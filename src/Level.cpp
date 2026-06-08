#include "Level.h"

#include "narrow_cast.h"
#include "PropKit.h"

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

            components.Body = RigidBody(bodyDef.Mass);
            components.Body->SetLinearVelocity(bodyDef.LinearVelocity);
        }

        if(nodeDef.Components.Collider.has_value())
        {
            const ColliderDef& colliderDef = *nodeDef.Components.Collider;
            struct Visitor
            {
                Collider operator()(const SphereDef& def) const
                {
                    return Collider{ Sphere{ def.Radius } };
                }

                Collider operator()(const BoxDef& def) const
                {
                    return Collider{ Box{ def.HalfExtents } };
                }

                Collider operator()(const CapsuleDef& def) const
                {
                    return Collider{ Capsule{ def.Radius, def.HalfHeight } };
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
                .ChildCount = narrow_cast<uint32_t>(nodeDef.Children.size()),
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

    return std::move(level);
}

Level::Level(std::vector<Node>&& nodes, std::vector<char>&& stringStorage)
    : m_Nodes(std::move(nodes)),
      m_StringStorage(std::move(stringStorage))
{
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
    const Node* node = GetNode(handle);
    MLG_CHECK(node);

    if(!node->FirstChildIndex.IsValid())
    {
        return std::span<const NodeHandle>{};
    }

    MLG_CHECKV(node->FirstChildIndex.Value() < m_Nodes.size(), "Invalid FirstChildIndex in node");
    MLG_CHECKV(node->FirstChildIndex.Value() + node->ChildCount <= m_Nodes.size(),
        "Invalid ChildCount in node");

    return std::span(m_NodeHandles).subspan(node->FirstChildIndex.Value(),
        node->ChildCount);
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

    return handle.m_Node;
}

Result<>
Level::UpdateLocalTransform(const NodeHandle& handle, const TrsTransformf& localTransform)
{
    Node* node = GetNode(handle);
    MLG_CHECKV(node);

    node->LocalTransform = localTransform;
    if(!node->ParentIndex.IsValid())
    {
        node->WorldTransform = localTransform.ToMatrix();
    }

    UpdateWorldTransforms({ node, 1 });

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
    return handle.IsValid() && !m_Nodes.empty() && handle.m_Node >= &m_Nodes.front() &&
           handle.m_Node <= &m_Nodes.back();
}

Level::Node*
Level::GetNode(const NodeHandle& handle)
{
    if(!MLG_VERIFY(IsInLevel(handle), "Node is not in level"))
    {
        return nullptr;
    }

    return const_cast<Node*>(handle.m_Node); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

void
Level::UpdateWorldTransforms(std::span<Node> nodes)
{
    for (Node& node : nodes)
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