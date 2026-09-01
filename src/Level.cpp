#include "Level.h"

#include "PhysicsTypes.h"
#include "PropKit.h"
#include "Result.h"
#include "SceneTypes.h"

#include <box3d/Box3D.h>
#include <box3d/collision.h>
#include <ranges>

namespace
{

Result<b3ShapeId>
AttachShapeToBody(const b3BodyId bodyId, const Mass& mass, const ColliderResource& colliderRsrc)
{
    b3ShapeDef shapeDef = b3DefaultShapeDef();
    constexpr float kDefaultRestitution = 0.8f;
    shapeDef.baseMaterial.restitution = kDefaultRestitution;

    switch(colliderRsrc.CollisionType)
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

    switch(colliderRsrc.ShapeType)
    {
        case ColliderShapeType::Sphere:
        {
            const ColliderResource::Sphere& sphereRsrc = colliderRsrc.GetSphere();
            const Vec3f center = sphereRsrc.Center;
            const b3Sphere sphere //
                {
                    .center = b3Pos{ .x = center.x, .y = center.y, .z = center.z },
                    .radius = sphereRsrc.Radius,
                };

            const float r3 = sphereRsrc.Radius * sphereRsrc.Radius * sphereRsrc.Radius;
            const float volume = (4.0f / 3.0f) * pi * r3;
            shapeDef.density = mass.Value() / volume;

            return b3CreateSphereShape(bodyId, &shapeDef, &sphere);
        }
        break;
        case ColliderShapeType::Box:
        {
            const ColliderResource::Box& boxRsrc = colliderRsrc.GetBox();
            const Vec3f halfExtents = boxRsrc.HalfExtents;
            const b3BoxHull dynamicBox = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
            const float volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
            shapeDef.density = mass.Value() / volume;

            return b3CreateHullShape(bodyId, &shapeDef, &dynamicBox.base);
        }
        break;
        case ColliderShapeType::Capsule:
        {
            const ColliderResource::Capsule& capsuleRsrc = colliderRsrc.GetCapsule();
            const float halfHeight = capsuleRsrc.HalfHeight;
            const Vec3f& center = capsuleRsrc.Center;
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
                    .radius = capsuleRsrc.Radius,
                };
            const float r2 = capsuleRsrc.Radius * capsuleRsrc.Radius;
            const float volume =
                ((4.0f / 3.0f) * pi * r2 * capsuleRsrc.Radius) + (4.0f * halfHeight * pi * r2);
            shapeDef.density = mass.Value() / volume;

            return b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        }
        break;
    }

    MLG_ERROR("Invalid bounding volume type");
    return Result<>::Fail;
}

Result<RigidBodyIdentifier>
CreateRigidBody(const LevelNode* node,
    const RigidBodyResource& rigidBodyRsrc,
    const std::span<const ColliderResource> colliderRsrcs,
    const WorldIdentifier worldId)
{
    b3BodyDef bodyDef = b3DefaultBodyDef();
    switch(rigidBodyRsrc.MotionType)
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
            MLG_ERROR("Invalid motion type for rigid body");
            return Result<>::Fail;
    }

    const Vec3f& pos = node->GetLocalTransform().T;
    const Vec4f rot = node->GetLocalTransform().R.ToVector();
    bodyDef.position = b3Pos{ .x = pos.x, .y = pos.y, .z = pos.z };
    bodyDef.rotation = b3Quat //
        {
            .v = { .x = rot.x, .y = rot.y, .z = rot.z },
            .s = rot.w,
        };

    const b3BodyId bodyId = b3CreateBody(b3LoadWorldId(worldId.GetValue()), &bodyDef);
    MLG_CHECK(b3Body_IsValid(bodyId), "Failed to create body for node");

    for(const ColliderResource& colliderRsrc : colliderRsrcs)
    {
        auto shapeId = AttachShapeToBody(bodyId, Mass(rigidBodyRsrc.Mass), colliderRsrc);
        MLG_CHECK(shapeId);
    }

    return RigidBodyIdentifier{ b3StoreBodyId(bodyId) };
}

Result<std::vector<LevelNode>>
CollectNodes(const ResourceBundle& resourceBundle)
{
    const std::span nodeRsrcs = resourceBundle.GetNodes();

    std::vector<LevelNode> nodes;
    nodes.reserve(nodeRsrcs.size());

    for(const auto& nodeRsrc : nodeRsrcs)
    {
        const LevelNode* parent = nullptr;
        if(nodeRsrc.ParentIndex != Resource::kInvalidIndex)
        {
            MLG_CHECKV(nodeRsrc.ParentIndex < nodes.size(), "Invalid parent index for node");
            parent = &nodes[nodeRsrc.ParentIndex];
        }

        TrsTransformf transform;
        transform.T = nodeRsrc.LocalPos;
        transform.R = UnitQuatf(nodeRsrc.LocalRot);
        transform.S = nodeRsrc.LocalScale;

        nodes.emplace_back(transform, parent);
    }

    return nodes;
}

Result<std::vector<ModelNode>>
CollectModelNodes(const ResourceBundle& resourceBundle,
    const PropKit& propKit,
    const std::span<const LevelNode>& nodes)
{
    const std::span modelInstanceRsrcs = resourceBundle.GetModelInstances();
    const std::span models = propKit.GetAllModels();

    uint32_t firstMeshInstanceIndex = 0;

    std::vector<ModelNode> modelNodes;
    modelNodes.reserve(modelInstanceRsrcs.size());
    for(const ModelInstanceResource& modelInstanceRsrc : modelInstanceRsrcs)
    {
        MLG_CHECKV(modelInstanceRsrc.NodeIndex < nodes.size(),
            "ModelInstanceResource has invalid NodeIndex");
        const LevelNode* levelNode = &nodes[modelInstanceRsrc.NodeIndex];

        MLG_CHECKV(modelInstanceRsrc.ModelIndex < models.size(),
            "ModelInstanceResource has invalid ModelIndex");
        const Model* model = &models[modelInstanceRsrc.ModelIndex];

        const size_t meshCount = model->GetMeshes().size();
        MLG_CHECKV(std::numeric_limits<uint32_t>::max() - firstMeshInstanceIndex > meshCount,
            "Exceeded maximum number of mesh instances");

        modelNodes.emplace_back(levelNode, model, firstMeshInstanceIndex);

        firstMeshInstanceIndex += meshCount;
    }

    return modelNodes;
}

Result<std::vector<PhysicsNode>>
CollectPhysicsNodes(const WorldIdentifier worldId,
    const ResourceBundle& resourceBundle,
    const std::span<LevelNode>& nodes)
{
    const std::span rigidBodyRsrcs = resourceBundle.GetRigidBodies();
    const std::span colliders = resourceBundle.GetColliders();
    std::vector<PhysicsNode> physicsNodes;
    physicsNodes.reserve(rigidBodyRsrcs.size());

    for(const RigidBodyResource& rigidBodyRsrc : rigidBodyRsrcs)
    {
        MLG_CHECKV(rigidBodyRsrc.NodeIndex < nodes.size(),
            "RigidBodyResource has invalid NodeIndex");
        LevelNode* levelNode = &nodes[rigidBodyRsrc.NodeIndex];

        MLG_CHECKV(rigidBodyRsrc.ColliderOffset + rigidBodyRsrc.ColliderCount <= colliders.size(),
            "RigidBodyResource has invalid Collider range");
        const std::span colliderSpan =
            colliders.subspan(rigidBodyRsrc.ColliderOffset, rigidBodyRsrc.ColliderCount);

        auto bodyId = CreateRigidBody(levelNode, rigidBodyRsrc, colliderSpan, worldId);
        MLG_CHECK(bodyId, "Failed to create rigid body for node");

        physicsNodes.emplace_back(levelNode, *bodyId);
    }

    return physicsNodes;
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

Result<Level>
Level::Create(const ResourceBundle& resourceBundle, const PropKit& propKit)
{
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.restitutionThreshold = 0.0f;
    worldDef.gravity = b3Vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f };

    const b3WorldId worldId = b3CreateWorld(&worldDef);
    MLG_ASSERT(b3World_IsValid(worldId));

    const WorldIdentifier worldIdentifier{ b3StoreWorldId(worldId) };

    auto levelNodes = CollectNodes(resourceBundle);
    MLG_CHECK(levelNodes, "Failed to collect level nodes");

    // Populate child nodes.
    const std::span nodeSpan = std::span(*levelNodes);

    for(const auto& [nodeRsrc, node] : std::views::zip(resourceBundle.GetNodes(), *levelNodes))
    {
        node.m_Children = nodeSpan.subspan(nodeRsrc.FirstChildIndex, nodeRsrc.ChildCount);
    }

    auto modelNodes = CollectModelNodes(resourceBundle, propKit, *levelNodes);
    MLG_CHECK(modelNodes, "Failed to collect model nodes");

    auto physicsNodes = CollectPhysicsNodes(worldIdentifier, resourceBundle, *levelNodes);
    MLG_CHECK(physicsNodes, "Failed to collect physics nodes");

    Level level(std::move(*levelNodes),
        std::move(*physicsNodes),
        std::move(*modelNodes),
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
    if(!MLG_VERIFY(&nodeRef >= m_Nodes.data() && &nodeRef <= &m_Nodes.back(),
           "Node is not in level"))
    {
        return nullptr;
    }

    const ptrdiff_t offset = &nodeRef - m_Nodes.data();
    if(!MLG_VERIFY(offset >= 0, "Node index out of range"))
    {
        return nullptr;
    }

    const size_t index = static_cast<size_t>(offset);
    if(!MLG_VERIFY(index < m_Nodes.size(), "Node index out of range"))
    {
        return nullptr;
    }

    return &m_Nodes[index];
}

void
Level::UpdateWorldTransforms(std::span<LevelNode> nodes)
{
    for(LevelNode& node : nodes)
    {
        const LevelNode* parent = node.GetParent();

        if(parent)
        {
            node.m_WorldTransform = parent->m_WorldTransform * node.m_LocalTransform.ToMatrix();
        }
        else
        {
            // No parent - the world transform is the same as the local transform.
            node.m_WorldTransform = node.m_LocalTransform.ToMatrix();
        }

        UpdateWorldTransforms(node.m_Children);
    }
}