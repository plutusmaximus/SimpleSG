#pragma once

#include "PhysicsTypes.h"
#include "SceneTypes.h"
#include "VecMath.h"

#include <span>
#include <string_view>

class Level;

class LevelNode
{
public:
    enum class Flags : uint8_t
    {
        None = 0,
        Active = 1 << 0,
        Visible = 1 << 1,
        All = Active | Visible
    };

    LevelNode(const TrsTransformf& localTransform, const LevelNode* parent)
        : m_LocalTransform(localTransform),
          m_Parent(parent)
    {
    }

    LevelNode() = delete;
    ~LevelNode() = default;
    LevelNode(const LevelNode&) = delete;
    LevelNode& operator=(const LevelNode&) = delete;
    LevelNode(LevelNode&&) = default;
    LevelNode& operator=(LevelNode&&) = default;

    bool IsActive() const { return (m_Flags & Flags::Active) == Flags::Active; }
    bool IsVisible() const { return (m_Flags & Flags::Visible) == Flags::Visible; }

    const TrsTransformf& GetLocalTransform() const { return m_LocalTransform; }
    const Mat44f& GetWorldTransform() const { return m_WorldTransform; }
    const Vec3f& GetLinearVelocity() const { return m_LinearVelocity; }
    const Vec3f& GetAngularVelocity() const { return m_AngularVelocity; }
    const LevelNode* GetParent() const { return m_Parent; }

    friend Flags operator|(const Flags a, const Flags b)
    {
        using U = std::underlying_type_t<Flags>;
        return static_cast<Flags>(static_cast<U>(a) | static_cast<U>(b));
    }

    friend Flags operator&(const Flags a, const Flags b)
    {
        using U = std::underlying_type_t<Flags>;
        return static_cast<Flags>(static_cast<U>(a) & static_cast<U>(b));
    }

    friend Flags operator~(const Flags a)
    {
        using U = std::underlying_type_t<Flags>;

        return static_cast<Flags>(static_cast<U>(~static_cast<U>(a)) & static_cast<U>(Flags::All));
    }

private:
    friend Level;

    TrsTransformf m_LocalTransform;
    Vec3f m_LinearVelocity{ 0 };
    Vec3f m_AngularVelocity{ 0 };
    Mat44f m_WorldTransform{ 1 };
    const LevelNode* m_Parent{ nullptr };
    std::span<LevelNode> m_Children;
    Flags m_Flags{ Flags::Active | Flags::Visible };
};

class PhysicsNode
{
public:
    PhysicsNode(LevelNode* node, const RigidBodyIdentifier rigidBodyId);

    PhysicsNode() = delete;
    ~PhysicsNode() = default;
    PhysicsNode(const PhysicsNode&) = delete;
    PhysicsNode& operator=(const PhysicsNode&) = delete;
    PhysicsNode(PhysicsNode&&) = default;
    PhysicsNode& operator=(PhysicsNode&&) = default;

    void ApplyImpulse(const Vec3f& impulse);

    void AddForce(const Vec3f& force);

    Vec3f GetPosition() const;

    UnitQuatf GetRotation() const;

    Vec3f GetLinearVelocity() const;

    void SetLinearVelocity(const Vec3f& velocity);

    // Radians per second
    Vec3f GetAngularVelocity() const;

    // Radians per second
    void SetAngularVelocity(const Vec3f& angularVelocity);

    float GetInverseMass() const;

private:
    friend Level;

    LevelNode* m_Node{ nullptr };
    RigidBodyIdentifier m_RigidBodyId;
};

class ModelNode
{
public:
    ModelNode(const LevelNode* node, const Model* model, const uint32_t firstMeshInstanceIndex);

    ModelNode() = delete;
    ~ModelNode() = default;
    ModelNode(const ModelNode&) = delete;
    ModelNode& operator=(const ModelNode&) = delete;
    ModelNode(ModelNode&&) = default;
    ModelNode& operator=(ModelNode&&) = default;

    const Mat44f& GetWorldTransform() const { return m_Node->GetWorldTransform(); }

    const BoundingBox& GetBoundingBox() const { return m_Model->GetBoundingBox(); }
    const BoundingSphere& GetBoundingSphere() const { return m_Model->GetBoundingSphere(); }

    std::span<const Mesh> GetMeshes() const { return m_Model->GetMeshes(); }

    uint32_t GetFirstMeshInstanceIndex() const { return m_FirstMeshInstanceIndex; }

    bool IsVisible() const { return m_Node->IsVisible(); }

private:
    friend Level;

    const LevelNode* m_Node{ nullptr };
    const Model* m_Model{ nullptr };
    uint32_t m_FirstMeshInstanceIndex{ 0 };
};