#include "Level.h"

#include "PhysicsTypes.h"
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
    const Level::Node* parentNode,
    std::vector<Level::Node>& nodes,
    StringArena& stringArena)
{
    MLG_CHECKV(nodes.capacity() >= nodes.size() + nodeDefs.size(),
        "Not enough capacity in nodes vector to collect nodes");

    const size_t initialNodeCount = nodes.size();

    // First add nodes from the current level.
    for(const auto& nodeDef : nodeDefs)
    {
        Level::Components components;

        if(nodeDef.Components.Model)
        {
            const ModelRef& modelRef = *nodeDef.Components.Model;

            MLG_CHECKV(!modelRef.Name.empty(), "ModelRef in node {} is empty", nodeDef.Name);

            const Model* model = propKit.GetModel(modelRef.Name);
            MLG_CHECK(model);

            components.Model = model;
        }

        if(nodeDef.Components.Body)
        {
            const RigidBodyDef& bodyDef = *nodeDef.Components.Body;

            MLG_CHECKV(bodyDef.Mass > 0,
                "RigidBodyDef in node {} has non-positive mass",
                nodeDef.Name);

            const BoundingVolumeDef& boundingVolumeDef = bodyDef.BoundingVolume;

            struct Visitor
            {
                BoundingVolume operator()(const SphereDef& def) const
                {
                    return BoundingVolume{ BoundingSphere{ def.Center, def.Radius } };
                }

                BoundingVolume operator()(const BoxDef& def) const
                {
                    const Vec3f p0 = def.Center - def.HalfExtents;
                    const Vec3f p1 = def.Center + def.HalfExtents;
                    return BoundingVolume{ BoundingBox{ p0, p1 } };
                }

                BoundingVolume operator()(const CapsuleDef& def) const
                {
                    return BoundingVolume{ BoundingCapsule{ def.Center, def.Radius, def.HalfHeight } };
                }
            };

            const BoundingVolume boundingVolume = std::visit(Visitor{}, boundingVolumeDef);

            components.Body = RigidBody(bodyDef.Mass, boundingVolume);
        }

        const Level::Node node //
            {
                .Name{stringArena.NewString(nodeDef.Name)},
                .LocalTransform{ nodeDef.Transform },
                .Components{ components },
                .Parent = parentNode,
            };

        nodes.emplace_back(node);
    }

    // Now add child nodes.

    const std::span<Level::Node> nodesSpan = std::span(nodes).subspan(initialNodeCount);

    for(auto&& [nodeDef, node] : std::views::zip(nodeDefs, nodesSpan))
    {
        if(nodeDef.Children.empty())
        {
            MLG_CHECKV(node.Components.Model || node.Components.Body,
                "Node {} has no model or body and no children",
                nodeDef.Name);
            continue;
        }

        const size_t firstChildIndex = nodes.size();

        MLG_CHECK(CollectNodes(nodeDef.Children, propKit, &node, nodes, stringArena));

        node.Children = std::span(nodes).subspan(firstChildIndex, nodeDef.Children.size());
    }

    return Result<>::Ok;
}
} // namespace

BoundingSphere
Level::Node::GetBoundingSphere() const
{
    MLG_ABORTIF(!Components.Model && !Components.Body && Children.empty(),
        "Node {} has no model, body, or children", Name);

    // Compute the combined bounding sphere of the child nodes.
    auto getChildrenBoundingSphere = [](const std::span<const Level::Node> children)
    {
        BoundingSphere bs = children.front().GetBoundingSphere();
        for(const auto& child : children.subspan(1))
        {
            bs = bs + child.GetBoundingSphere();
        }

        return bs;
    };

    // Compute the combined bounding sphere of this node's model and/or body.
    auto getLocalBoundingSphere = [](const Level::Node& node)
    {
        if(node.Components.Model)
        {
            BoundingSphere bs = (*node.Components.Model)->GetBoundingSphere();

            if(node.Components.Body)
            {
                bs = bs + node.Components.Body->GetBoundingSphere();
            }

            return bs;
        }

        return node.Components.Body->GetBoundingSphere();
    };

    if(!Children.empty())
    {
        // Transform bounding spheres to the node's local space.
        
        BoundingSphere bs = LocalTransform * getChildrenBoundingSphere(Children);

        if(Components.Model || Components.Body)
        {
            const BoundingSphere localBs = LocalTransform * getLocalBoundingSphere(*this);
            bs = bs + localBs;
        }

        return bs;
    }

    return LocalTransform * getLocalBoundingSphere(*this);
}

Result<Level>
Level::Create(const LevelDef& levelDef, const PropKit& propKit)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);
    const size_t totalStringSize = CalculateTotalStringSize(levelDef.NodeDefs);

    std::vector<Node> nodes;
    nodes.reserve(nodeCount);
    StringArena stringArena(totalStringSize);

    // Flatten nodes into breadth-first order.
    MLG_CHECK(CollectNodes(levelDef.NodeDefs, propKit, nullptr, nodes, stringArena));

    Level level(std::move(nodes), std::move(stringArena));

    return std::move(level);
}

Level::Level(std::vector<Node>&& nodes, StringArena&& stringArena)
    : m_Nodes(std::move(nodes)),
      m_StringArena(std::move(stringArena))
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

    // Calculate world transforms for all nodes.
    for(auto& node : m_Nodes)
    {
        if(node.Parent)
        {
            node.WorldTransform = node.Parent->WorldTransform * node.LocalTransform.ToMatrix();
        }
        else
        {
            node.WorldTransform = node.LocalTransform.ToMatrix();
        }
    }

    m_RootNodes = std::span(m_Nodes).subspan(0, rootNodeCount);
}

Result<const Level::Node*>
Level::GetNode(std::initializer_list<std::string_view> path) const
{
    return GetNode(std::span{ path });
}

Result<>
Level::UpdateLocalTransform(const Node& nodeRef, const TrsTransformf& localTransform)
{
    Node* node = GetNode(nodeRef);
    MLG_CHECKV(node, "Invalid or nonexistent node passed to UpdateLocalTransform");

    node->LocalTransform = localTransform;

    // Update the world transform of the node and all its descendants.
    // TODO(KB) - this defer updating of world transforms.
    UpdateWorldTransforms(std::span(node, 1));

    return Result<>::Ok;
}

void
Level::SetActive(const Node& nodeRef, bool active)
{
    Node* node = GetNode(nodeRef);
    if(!MLG_VERIFY(node, "Invalid or nonexistent node passed to SetActive"))
    {
        return;
    }

    node->Flags = active ? (node->Flags | NodeFlags::Active) : (node->Flags & ~NodeFlags::Active);

    for(const auto& childNode : node->Children)
    {
        SetActive(childNode, active);
    }
}

void
Level::SetVisible(const Node& nodeRef, bool visible)
{
    Node* node = GetNode(nodeRef);

    if(!MLG_VERIFY(node, "Invalid or nonexistent node passed to SetVisible"))
    {
        return;
    }

    node->Flags = visible ? (node->Flags | NodeFlags::Visible) : (node->Flags & ~NodeFlags::Visible);

    for(const auto& childNode : node->Children)
    {
        SetVisible(childNode, visible);
    }
}

// private:

Level::Node*
Level::GetNode(const Node& nodeRef)
{
    if(!MLG_VERIFY(IsInLevel(nodeRef), "Node is not in level"))
    {
        return nullptr;
    }

    const ptrdiff_t index = &nodeRef - m_Nodes.data();

    if(!MLG_VERIFY(index >= 0 && static_cast<size_t>(index) < m_Nodes.size(),
            "Node index out of range"))
    {
        return nullptr;
    }

    return &m_Nodes[static_cast<size_t>(index)];
}

bool
Level::IsInLevel(const Node& nodeRef) const
{
    return &nodeRef >= m_Nodes.data() && &nodeRef <= &m_Nodes.back();
}

void
Level::UpdateWorldTransforms(std::span<const Node> nodes)
{
    for (const Node& nodeRef : nodes)
    {
        Node* node = GetNode(nodeRef);
        if(!MLG_VERIFY(node, "Invalid node handle found while updating world transforms"))
        {
            continue;
        }
        
        if(node->Parent)
        {
            node->WorldTransform = node->Parent->WorldTransform * node->LocalTransform.ToMatrix();
        }
        else
        {
            // No parent - the world transform is the same as the local transform.
            node->WorldTransform = node->LocalTransform.ToMatrix();
        }

        if(!node->Children.empty())
        {
            UpdateWorldTransforms(node->Children);
        }
    }
}