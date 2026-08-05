#include "PhysicsLevel2.h"

#include <box3d/Box3D.h>
#include <box3d/collision.h>
#include <variant>

namespace
{
Result<b3ShapeId>
CreateShape(const b3BodyId bodyId, const RigidBody& body, const Collider& collider)
{
    constexpr float pi = std::numbers::pi_v<float>;
    const float radius = collider.GetBoundingSphere().GetRadius();
    const float density = body.GetMass().Value() / (4.0f / 3.0f * pi * radius * radius * radius);

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.density = density;
    constexpr float kDefaultRestitution = 0.8f;
    shapeDef.baseMaterial.restitution = kDefaultRestitution;

    switch(collider.GetCollisionType())
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

    struct Visitor
    {
        b3BodyId BodyId;
        b3ShapeDef ShapeDef;

        b3ShapeId operator()(const BoundingSphere& boundingSphere) const
        {
            const Vec3f center = boundingSphere.GetCenter();
            const b3Sphere sphere //
                {
                    .center = b3Pos{ .x = center.x, .y = center.y, .z = center.z },
                    .radius = boundingSphere.GetRadius(),
                };

            return b3CreateSphereShape(BodyId, &ShapeDef, &sphere);
        }

        b3ShapeId operator()(const BoundingBox& boundingBox) const
        {
            //const Vec3f center = boundingBox.GetCenter();
            const Vec3f halfExtents = boundingBox.GetHalfExtents();                
            const b3BoxHull dynamicBox = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
            return b3CreateHullShape(BodyId, &ShapeDef, &dynamicBox.base);
        }

        b3ShapeId operator()(const BoundingCapsule& boundingCapsule) const
        {
            const float halfHeight = boundingCapsule.GetHalfHeight();
            const Vec3f center = boundingCapsule.GetCenter();
            const b3Capsule capsule //
                {
                    .center1 = b3Pos{ .x = center.x,
                                    .y = center.y - halfHeight,
                                    .z = center.z },
                    .center2 = b3Pos{ .x = center.x,
                                    .y = center.y + halfHeight,
                                    .z = center.z },
                    .radius = boundingCapsule.GetRadius(),
                };
            return b3CreateCapsuleShape(BodyId, &ShapeDef, &capsule);
        }
    };

    const Visitor visitor{ .BodyId = bodyId, .ShapeDef = shapeDef };
    const b3ShapeId shapeId = std::visit(visitor, collider.GetBoundingVolume().GetVolume());

    return shapeId;
}
} // namespace

Result<PhysicsLevel2>
PhysicsLevel2::Create(const Level& level)
{
    size_t bodyCount = 0;
    for(const auto& node : level.GetAllNodes())
    {
        if(node.Components.Body)
        {
            ++bodyCount;
        }
    }

    std::vector<NodeAndIndex> nodeIndexMap;
    std::vector<const Level::Node*> nodes;
    std::vector<b3ShapeId> shapeIds;
    std::vector<b3BodyId> bodyIds;

    nodeIndexMap.reserve(bodyCount);
    nodes.reserve(bodyCount);
    bodyIds.reserve(bodyCount);
    shapeIds.reserve(bodyCount);

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.restitutionThreshold = 0.0f;
    worldDef.gravity = b3Vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f };

    const b3WorldId worldId = b3CreateWorld(&worldDef);
    MLG_ASSERT(b3World_IsValid(worldId));

    for(const auto& node : level.GetAllNodes())
    {
        const std::optional<RigidBody>& optBody = node.Components.Body;

        if(!optBody)
        {
            continue;
        }

        const RigidBody& body = *optBody;

        // m_NodeIndexMap is ordered by node pointer, so we can use binary search to find the index
        // of a node.
        nodeIndexMap.emplace_back(&node, nodes.size());

        nodes.emplace_back(&node);

        b3BodyDef bodyDef = b3DefaultBodyDef();
        switch(body.GetMotionType())
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
                MLG_ERROR("Invalid motion type for node {}", node.Name);
                return Result<>::Fail;
        }

        const Vec3f pos = node.WorldTransform[3].xyz();
        bodyDef.position = b3Pos{ .x = pos.x, .y = pos.y, .z = pos.z };

        const b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);
        MLG_ASSERT(b3Body_IsValid(bodyId));

        for(const Collider& collider : body.GetColliders())
        {
            auto shapeId = CreateShape(bodyId, body, collider);
            MLG_CHECK(shapeId, "Failed to create shape for node {}", node.Name);

            MLG_ASSERT(b3Shape_IsValid(*shapeId));
            shapeIds.emplace_back(*shapeId);
        }

        bodyIds.emplace_back(bodyId);
    }

    return PhysicsLevel2(nodeIndexMap, nodes, worldId, shapeIds, bodyIds);
}

void
PhysicsLevel2::Update(const float timeStep)
{
    constexpr int kSubStepCount = 4;
    b3World_Step( m_WorldId, timeStep, kSubStepCount );
}

void
PhysicsLevel2::ApplyImpulse(const Level::Node* node, const Vec3f& impulse)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        const b3Vec3 impulseVec //
            {
                .x = impulse.x,
                .y = impulse.y,
                .z = impulse.z,
            };
        b3Body_ApplyLinearImpulseToCenter(m_BodyIds[index], impulseVec, true);
    }
}

void
PhysicsLevel2::AddForce(const Level::Node* node, const Vec3f& force)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        const b3Vec3 forceVec //
            {
                .x = force.x,
                .y = force.y,
                .z = force.z,
            };
        b3Body_ApplyForceToCenter(m_BodyIds[index], forceVec, true);
    }
}

//DO NOT SUBMIT
//NOLINTBEGIN

Result<>
PhysicsLevel2::SyncToLevel(Level& level)
{
    for(size_t index = 0; index < m_BodyIds.size(); ++index)
    {
        const b3Pos pos = b3Body_GetPosition(m_BodyIds[index]);
        const b3Quat rot = b3Body_GetRotation(m_BodyIds[index]);
        const Level::Node* node = m_Nodes[index];
        MLG_ASSERT(node, "Node pointer is null");
        TrsTransformf trs = node->LocalTransform;
        trs.T = Vec3f{ pos.x, pos.y, pos.z };
        trs.R = UnitQuatf{ rot.v.x, rot.v.y, rot.v.z, rot.s };
        MLG_CHECK(level.UpdateLocalTransform(*node, trs));
    }

    return Result<>::Ok;
}

Vec3f
PhysicsLevel2::GetPosition(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        b3Pos pos = b3Body_GetPosition(m_BodyIds[index]);
        return Vec3f{ pos.x, pos.y, pos.z };
    }
    return Vec3f{ 0 };
}

Vec3f
PhysicsLevel2::GetLinearVelocity(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        b3Vec3 vel = b3Body_GetLinearVelocity(m_BodyIds[index]);
        return Vec3f{ vel.x, vel.y, vel.z };
    }
    return Vec3f{ 0 };
}

float
PhysicsLevel2::GetRadius(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return b3Shape_GetSphere(m_ShapeIds[index]).radius;
    }
    return 0.0f;
}

float
PhysicsLevel2::GetInverseMass(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return b3Body_GetInverseMass(m_BodyIds[index]);
    }
    return 0.0f;
}

//NOLINTEND

void
PhysicsLevel2::GetPositions(VVec3& positions) const
{
    MLG_ASSERT(positions.X.size() == m_BodyIds.size());
    MLG_ASSERT(positions.Y.size() == m_BodyIds.size());
    MLG_ASSERT(positions.Z.size() == m_BodyIds.size());

    const size_t count =
        std::min({ positions.X.size(), positions.Y.size(), positions.Z.size(), m_BodyIds.size() });

    for(size_t index = 0; index < count; ++index)
    {
        const b3Pos pos = b3Body_GetPosition(m_BodyIds[index]);
        positions.X[index] = pos.x;
        positions.Y[index] = pos.y;
        positions.Z[index] = pos.z;
    }
}

void
PhysicsLevel2::GetLinearVelocities(VVec3& linearVelocities) const
{
    MLG_ASSERT(linearVelocities.X.size() == m_BodyIds.size());
    MLG_ASSERT(linearVelocities.Y.size() == m_BodyIds.size());
    MLG_ASSERT(linearVelocities.Z.size() == m_BodyIds.size());

    const size_t count =
        std::min({ linearVelocities.X.size(), linearVelocities.Y.size(), linearVelocities.Z.size(), m_BodyIds.size() });

    for(size_t index = 0; index < count; ++index)
    {
        const b3Vec3 vel = b3Body_GetLinearVelocity(m_BodyIds[index]);
        linearVelocities.X[index] = vel.x;
        linearVelocities.Y[index] = vel.y;
        linearVelocities.Z[index] = vel.z;
    }
}

void
PhysicsLevel2::GetInverseMasses(std::span<float>& invMasses) const
{
    MLG_ASSERT(invMasses.size() == m_BodyIds.size());

    const size_t count = std::min(invMasses.size(), m_BodyIds.size());

    for(size_t index = 0; index < count; ++index)
    {
        invMasses[index] = b3Body_GetInverseMass(m_BodyIds[index]);
    }
}

void
PhysicsLevel2::SetLinearVelocity(const Level::Node* node, const Vec3f& velocity)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        const b3Vec3 vel //
            {
                .x = velocity.x,
                .y = velocity.y,
                .z = velocity.z,
            };
        b3Body_SetLinearVelocity(m_BodyIds[index], vel);
    }
}

void
PhysicsLevel2::SetAngularVelocity(const Level::Node* node, const Vec3f& angularVelocity)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        const b3Vec3 angVel //
            {
                .x = angularVelocity.x,
                .y = angularVelocity.y,
                .z = angularVelocity.z,
            };
        b3Body_SetAngularVelocity(m_BodyIds[index], angVel);
    }
}

// private:
    
size_t
PhysicsLevel2::GetNodeIndex(const Level::Node* node) const
{
    MLG_ASSERT(node, "Node pointer is null");

    const size_t offset = static_cast<size_t>(node - m_NodeIndexMap.front().GetNode());
    if(offset < m_NodeIndexMap.size() && MLG_VERIFY(m_NodeIndexMap[offset].GetNode() == node))
    {
        return m_NodeIndexMap[offset].GetIndex();
    }
    
    return NodeAndIndex::kInvalidIndex;
}