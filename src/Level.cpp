#include "Level.h"

#include "PhysicsTypes.h"
#include "PropKit.h"

#include <box3d/Box3D.h>
#include <box3d/collision.h>
#include <ranges>

namespace
{
template<typename T>
size_t
CountNodes(const T& nodeDefs)
{
    using NodeDef = std::ranges::range_value_t<T>;

    static_assert(std::same_as<NodeDef, LevelNodeDef> || std::same_as<NodeDef, RootNodeDef>,
        "CountNodes requires a range of LevelNodeDef or RootNodeDef");

    size_t count = nodeDefs.size();
    for(const auto& nodeDef : nodeDefs)
    {
        count += CountNodes(nodeDef.Children);
    }

    return count;
}
template<typename T>
size_t
CountModels(const T& nodeDefs)
{
    using NodeDef = std::ranges::range_value_t<T>;

    static_assert(std::same_as<NodeDef, LevelNodeDef> || std::same_as<NodeDef, RootNodeDef>,
        "CountModels requires a range of LevelNodeDef or RootNodeDef");

    size_t count = 0;
    for(const auto& nodeDef : nodeDefs)
    {
        if(nodeDef.Model)
        {
            ++count;
        }

        count += CountModels(nodeDef.Children);
    }

    return count;
}

size_t
CountBodies(std::span<const RootNodeDef> nodeDefs)
{
    size_t count = 0;
    for(const auto& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            ++count;
        }

        // We don't count bodies in children because rigid bodies can only be attched to root nodes.
    }

    return count;
}

Result<b3ShapeId>
AttachShapeToBody(const b3BodyId bodyId, const Mass& mass, const ColliderDef& colliderDef)
{
    b3ShapeDef shapeDef = b3DefaultShapeDef();
    constexpr float kDefaultRestitution = 0.8f;
    shapeDef.baseMaterial.restitution = kDefaultRestitution;

    switch(colliderDef.CollisionType)
    {
        case CollisionType::Block:
            shapeDef.isSensor = false;
            shapeDef.enableSensorEvents = false;
            break;
        case CollisionType::Trigger:
            shapeDef.isSensor = true;
            shapeDef.enableSensorEvents = true;
            break;
        default:
            MLG_ERROR("Invalid collision type");
            return Result<>::Fail;
    }

    constexpr float pi = std::numbers::pi_v<float>;

    switch(colliderDef.BoundingVolume.GetType())
    {
        case BoundingVolumeDef::Type::Sphere:
        {
            const SphereDef& sphereDef = colliderDef.BoundingVolume.GetSphereDef();
            const Vec3f center = sphereDef.Center;
            const b3Sphere sphere //
                {
                    .center = b3Pos{ .x = center.x, .y = center.y, .z = center.z },
                    .radius = sphereDef.Radius,
                };

            const float r3 = sphereDef.Radius * sphereDef.Radius * sphereDef.Radius;
            const float volume = (4.0f / 3.0f) * pi * r3;
            shapeDef.density = mass.Value() / volume;

            return b3CreateSphereShape(bodyId, &shapeDef, &sphere);
        }
        break;
        case BoundingVolumeDef::Type::Box:
        {
            const BoxDef& boxDef = colliderDef.BoundingVolume.GetBoxDef();
            const Vec3f halfExtents = boxDef.HalfExtents;
            const b3BoxHull dynamicBox = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
            const float volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
            shapeDef.density = mass.Value() / volume;

            return b3CreateHullShape(bodyId, &shapeDef, &dynamicBox.base);
        }
        break;
        case BoundingVolumeDef::Type::Capsule:
        {
            const CapsuleDef& capsuleDef = colliderDef.BoundingVolume.GetCapsuleDef();
            const float halfHeight = capsuleDef.HalfHeight;
            const Vec3f& center = capsuleDef.Center;
            const b3Capsule capsule //
                {
                    .center1 //
                    {
                        .x = center.x,
                        .y = center.y - halfHeight,
                        .z = center.z,
                    },
                    .center2 //
                    {
                        .x = center.x,
                        .y = center.y + halfHeight,
                        .z = center.z,
                    },
                    .radius = capsuleDef.Radius,
                };
            const float r2 = capsuleDef.Radius * capsuleDef.Radius;
            const float volume =
                ((4.0f / 3.0f) * pi * r2 * capsuleDef.Radius) + (4.0f * halfHeight * pi * r2);
            shapeDef.density = mass.Value() / volume;

            return b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        }
        break;
    }

    MLG_ERROR("Invalid bounding volume type");
    return Result<>::Fail;
}

Result<RigidBodyIdentifier>
CreateRigidBody(
    const RootNodeDef& nodeDef, const RigidBodyDef& rbodyDef, const WorldIdentifier worldId)
{
    b3BodyDef bodyDef = b3DefaultBodyDef();
    switch(rbodyDef.MotionType)
    {
        case MotionType::Static:
            bodyDef.type = b3_staticBody;
            break;
        case MotionType::Kinematic:
            bodyDef.type = b3_kinematicBody;
            break;
        case MotionType::Dynamic:
            bodyDef.type = b3_dynamicBody;
            break;
        default:
            MLG_ERROR("Invalid motion type for node {}", nodeDef.Name);
            return Result<>::Fail;
    }

    const Vec3f& pos = nodeDef.Transform.T;
    const Vec4f rot = nodeDef.Transform.R.ToVector();
    bodyDef.position = b3Pos{ .x = pos.x, .y = pos.y, .z = pos.z };
    bodyDef.rotation = b3Quat //
        {
            .v = { .x = rot.x, .y = rot.y, .z = rot.z },
            .s = rot.w,
        };

    const b3BodyId bodyId = b3CreateBody(b3LoadWorldId(worldId.GetValue()), &bodyDef);
    MLG_CHECK(b3Body_IsValid(bodyId), "Failed to create body for node {}", nodeDef.Name);

    for(const ColliderDef& colliderDef : rbodyDef.Colliders)
    {
        auto shapeId = AttachShapeToBody(bodyId, rbodyDef.Mass, colliderDef);
        MLG_CHECK(shapeId);
    }

    return RigidBodyIdentifier{ b3StoreBodyId(bodyId) };
}

b3BodyId
GetBodyId(const RigidBodyIdentifier rigidBodyId)
{
    MLG_ASSERT(rigidBodyId.IsValid(), "RigidBodyIdentifier must be valid");
    const b3BodyId bodyId = b3LoadBodyId(rigidBodyId.GetValue());
    MLG_ASSERT(b3Body_IsValid(bodyId), "Node does not have a valid body id");
    return bodyId;
}
} // namespace

// Collect nodes in breadth-first order.
// Parents come before children, siblings are contiguous.
template<typename T>
Result<>
Level::CollectNodes(T nodeDefs,
    const PropKit& propKit,
    const WorldIdentifier worldId,
    const LevelNode* parentNode,
    std::vector<LevelNode>& nodes,
    std::vector<PhysicsNode>& physicsNodes,
    std::vector<ModelNode>& modelNodes)
{
    MLG_CHECKV(nodes.capacity() >= nodes.size() + nodeDefs.size(),
        "Not enough capacity in nodes vector to collect nodes");

    const size_t initialNodeCount = nodes.size();

    // Add nodes from the current level.
    for(const auto& nodeDef : nodeDefs)
    {
        using NodeDefType = std::remove_cvref_t<decltype(nodeDef)>;

        if(nodeDef.Children.empty())
        {
            bool uselessNode = false;
            if constexpr(std::same_as<NodeDefType, RootNodeDef>)
            {
                uselessNode = !nodeDef.Model && !nodeDef.Body;
            }
            else
            {
                uselessNode = !nodeDef.Model;
            }

            // Useless node - return an error.
            MLG_CHECKV(!uselessNode, "Node {} has no model or body and no children", nodeDef.Name);
        }

        nodes.emplace_back(nodeDef.Transform, parentNode);

        if(nodeDef.Model)
        {
            const ModelRef& modelRef = *nodeDef.Model;

            MLG_CHECKV(!modelRef.Name.empty(), "ModelRef in node {} is empty", nodeDef.Name);

            const Model* model = propKit.GetModel(modelRef.Name);
            MLG_CHECK(model);

            modelNodes.emplace_back(ModelNode(&nodes.back(), model));
        }

        // For root nodes, create a rigid body if specified.
        if constexpr(std::same_as<NodeDefType, RootNodeDef>)
        {
            if(nodeDef.Body)
            {
                const RigidBodyDef& rigidBodyDef = *nodeDef.Body;

                MLG_CHECKV(rigidBodyDef.Mass > 0,
                    "RigidBodyDef in node {} has non-positive mass",
                    nodeDef.Name);

                auto bodyId = CreateRigidBody(nodeDef, rigidBodyDef, worldId);
                MLG_CHECK(bodyId, "Failed to create rigid body for node {}", nodeDef.Name);

                physicsNodes.push_back(PhysicsNode{ &nodes.back(), *bodyId });
            }
        }
    }

    // Now add child nodes.

    const std::span<LevelNode> nodesSpan = std::span(nodes).subspan(initialNodeCount);

    for(auto&& [nodeDef, node] : std::views::zip(nodeDefs, nodesSpan))
    {
        if(nodeDef.Children.empty())
        {
            continue;
        }

        const size_t firstChildIndex = nodes.size();

        MLG_CHECK(CollectNodes(nodeDef.Children,
            propKit,
            worldId,
            &node,
            nodes,
            physicsNodes,
            modelNodes));

        node.m_Children = std::span(nodes).subspan(firstChildIndex, nodeDef.Children.size());
    }

    return Result<>::Ok;
}

Result<Level>
Level::Create(const LevelDef& levelDef, const PropKit& propKit)
{
    const size_t nodeCount = CountNodes(levelDef.NodeDefs);
    const size_t bodyCount = CountBodies(levelDef.NodeDefs);
    const size_t modelCount = CountModels(levelDef.NodeDefs);

    std::vector<LevelNode> nodes;
    std::vector<PhysicsNode> physicsNodes;
    std::vector<ModelNode> modelNodes;
    nodes.reserve(nodeCount);
    physicsNodes.reserve(bodyCount);
    modelNodes.reserve(modelCount);

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.restitutionThreshold = 0.0f;
    worldDef.gravity = b3Vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f };

    const b3WorldId worldId = b3CreateWorld(&worldDef);
    MLG_ASSERT(b3World_IsValid(worldId));

    // Flatten nodes into breadth-first order.
    MLG_CHECK(CollectNodes(levelDef.NodeDefs,
        propKit,
        WorldIdentifier{ b3StoreWorldId(worldId) },
        nullptr,
        nodes,
        physicsNodes,
        modelNodes));

    const WorldIdentifier worldIdentifier{ b3StoreWorldId(worldId) };

    Level level(std::move(nodes),
        std::move(physicsNodes),
        std::move(modelNodes),
        worldIdentifier);

    return std::move(level);
}

Level::Level(std::vector<LevelNode>&& nodes,
    std::vector<PhysicsNode>&& physicsNodes,
    std::vector<ModelNode>&& modelNodes,
    const WorldIdentifier worldId)
    : m_Nodes(std::move(nodes)),
      m_PhysicsNodes(std::move(physicsNodes)),
      m_ModelNodes(std::move(modelNodes)),
      m_WorldId(worldId)
{
    size_t rootNodeCount = 0;

    // Count root nodes.
    // Nodes are stored in breadth-first order, so all root nodes will be at the beginning
    // of the vector.
    for(const auto& node : m_Nodes)
    {
        if(node.m_Parent)
        {
            // No more root nodes after this.
            break;
        }

        ++rootNodeCount;
    }

    m_RootNodes = std::span(m_Nodes).subspan(0, rootNodeCount);

    UpdateWorldTransforms(m_RootNodes);
}

Level::~Level()
{
    if(m_WorldId.IsValid())
    {
        const b3WorldId worldId = b3LoadWorldId(m_WorldId.GetValue());
        MLG_ASSERT(b3World_IsValid(worldId));
        b3DestroyWorld(worldId);

        m_WorldId = {};
    }
}

void
Level::Update(const float timeStep)
{
    constexpr int kSubStepCount = 4;
    const b3WorldId worldId = b3LoadWorldId(m_WorldId.GetValue());
    MLG_ASSERT(b3World_IsValid(worldId));
    b3World_Step(worldId, timeStep, kSubStepCount);

    // Sync to level nodes.
    for(const PhysicsNode& physicsNode : m_PhysicsNodes)
    {
        LevelNode* node = physicsNode.m_Node;
        const b3BodyId bodyId = GetBodyId(physicsNode.m_RigidBodyId);
        const b3Pos pos = b3Body_GetPosition(bodyId);
        const b3Quat rot = b3Body_GetRotation(bodyId);
        const b3Vec3 vel = b3Body_GetLinearVelocity(bodyId);
        const b3Vec3 angVel = b3Body_GetAngularVelocity(bodyId);

        // Rigid bodies can only be attached to root nodes.
        // Updating the local transform of a root node is equivalent to updating its world
        // transform.
        node->m_LocalTransform.T = Vec3f{ pos.x, pos.y, pos.z };
        node->m_LocalTransform.R = UnitQuatf{ rot.v.x, rot.v.y, rot.v.z, rot.s };
        node->m_LinearVelocity = Vec3f{ vel.x, vel.y, vel.z };
        node->m_AngularVelocity = Vec3f{ angVel.x, angVel.y, angVel.z };
    }

    UpdateWorldTransforms(m_RootNodes);
}

void
Level::SetActive(const LevelNode& nodeRef, bool active)
{
    LevelNode* node = GetNode(nodeRef);
    if(!MLG_VERIFY(node, "Invalid or nonexistent node passed to SetActive"))
    {
        return;
    }

    node->m_Flags = active ? (node->m_Flags | LevelNode::Flags::Active)
                           : (node->m_Flags & ~LevelNode::Flags::Active);

    for(const auto& childNode : node->m_Children)
    {
        SetActive(childNode, active);
    }
}

void
Level::SetVisible(const LevelNode& nodeRef, bool visible)
{
    LevelNode* node = GetNode(nodeRef);

    if(!MLG_VERIFY(node, "Invalid or nonexistent node passed to SetVisible"))
    {
        return;
    }

    node->m_Flags = visible ? (node->m_Flags | LevelNode::Flags::Visible)
                            : (node->m_Flags & ~LevelNode::Flags::Visible);

    for(const auto& childNode : node->m_Children)
    {
        SetVisible(childNode, visible);
    }
}

// private:

LevelNode*
Level::GetNode(const LevelNode& nodeRef)
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
Level::IsInLevel(const LevelNode& nodeRef) const
{
    return &nodeRef >= m_Nodes.data() && &nodeRef <= &m_Nodes.back();
}

void
Level::UpdateWorldTransforms(std::span<LevelNode> nodes)
{
    for(LevelNode& node : nodes)
    {
        if(node.m_Parent)
        {
            node.m_WorldTransform =
                node.m_Parent->m_WorldTransform * node.m_LocalTransform.ToMatrix();
        }
        else
        {
            // No parent - the world transform is the same as the local transform.
            node.m_WorldTransform = node.m_LocalTransform.ToMatrix();
        }

        if(!node.m_Children.empty())
        {
            UpdateWorldTransforms(node.m_Children);
        }
    }
}