#pragma once

#include "PhysicsTypes.h"
#include "StringArena.h"
#include "VecMath.h"

#include <span>
#include <string_view>

class Level;
class Model;

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

    LevelNode(const StringHandle& name,
        const TrsTransformf& localTransform,
        const LevelNode* parent)
        : m_Name(name),
          m_LocalTransform(localTransform),
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

    std::string_view GetName() const { return m_Name; }
    const TrsTransformf& GetLocalTransform() const { return m_LocalTransform; }
    const Mat44f& GetWorldTransform() const { return m_WorldTransform; }
    const Vec3f& GetLinearVelocity() const { return m_LinearVelocity; }
    const Vec3f& GetAngularVelocity() const { return m_AngularVelocity; }
    const LevelNode* GetParent() const { return m_Parent; }
    std::span<const LevelNode> GetChildren() const { return m_Children; }

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

    StringHandle m_Name;
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

    PhysicsNode(LevelNode* node, const RigidBodyIdentifier rigidBodyId)
        : m_Node(node),
          m_RigidBodyId(rigidBodyId)
    {
        MLG_ASSERT(node, "PhysicsNode must be associated with a valid LevelNode");
        MLG_ASSERT(!node->GetParent(), "PhysicsNode must be associated with a root LevelNode");
        MLG_ASSERT(rigidBodyId.IsValid(),
            "PhysicsNode must be associated with a LevelNode that has a valid rigid body ID");
    }

    LevelNode* m_Node{ nullptr };
    RigidBodyIdentifier m_RigidBodyId;
};

class ModelNode
{
public:
    ModelNode() = delete;
    ~ModelNode() = default;
    ModelNode(const ModelNode&) = delete;
    ModelNode& operator=(const ModelNode&) = delete;
    ModelNode(ModelNode&&) = default;
    ModelNode& operator=(ModelNode&&) = default;

    const Model* GetModel() const { return m_Model; }

    const Mat44f& GetWorldTransform() const { return m_Node->GetWorldTransform(); }

    bool IsVisible() const { return m_Node->IsVisible(); }

private:
    friend Level;

    ModelNode(LevelNode* node, const Model* model)
        : m_Node(node),
          m_Model(model)
    {
        MLG_ASSERT(node, "ModelNode must be associated with a valid LevelNode");
        MLG_ASSERT(model, "ModelNode must be associated with a valid Model");
    }

    LevelNode* m_Node{ nullptr };
    const Model* m_Model{ nullptr };
};