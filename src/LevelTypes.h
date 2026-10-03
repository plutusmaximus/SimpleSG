#pragma once

#include "BoundingVolumes.h"
#include "Color.h"
#include "FilePath.h"
#include "FixedString.h"
#include "PhysicsTypes.h"
#include "VecMath.h"

#include <span>
#include <vector>
#include <optional>

/// Definitions for level structure, including materials, meshes, models, and nodes.
/// Used to declaratively define the structure and properties of a level.

namespace SceneDefs
{
constexpr size_t kNameStorageSize = 32;
using NameString = FixedString<kNameStorageSize>;

using FilePathString = FixedString<RelativeFilePath::kStorageSize>;

} // namespace SceneDefs

struct MaterialDef final
{
    SceneDefs::FilePathString BaseTexturePath;
    RgbaColorf Color{ 1, 1, 1, 1 };
    float Metalness{ 0.0f };
    float Roughness{ 0.0f };

    // Used to deduplicate materials based on their properties.
    friend auto operator<=>(const MaterialDef& lhs, const MaterialDef& rhs)
    {
        if(auto cmp = lhs.BaseTexturePath <=> rhs.BaseTexturePath; cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Color.r, rhs.Color.r); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.g, rhs.Color.g); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.b, rhs.Color.b); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.a, rhs.Color.a); cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Metalness, rhs.Metalness); cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Roughness, rhs.Roughness); cmp != 0)
        {
            return cmp;
        }

        return std::strong_ordering::equal;
    }
};

struct MeshDef final
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    MaterialDef MaterialDef;
};

struct ModelDef final
{
    SceneDefs::NameString Name;
    std::vector<MeshDef> MeshDefs;
};

struct ModelRef final
{
    SceneDefs::NameString Name;
};

struct BoxDef final
{
    Vec3f Center{ 0 };
    Vec3f HalfExtents{ 0 };
};

struct CapsuleDef final
{
    Vec3f Center{ 0 };
    float Radius{ 0 };
    float HalfHeight{ 0 };
};

struct SphereDef final
{
    Vec3f Center{ 0 };
    float Radius{ 0 };
};

struct ColliderShapeDef final
{
    ColliderShapeDef() = delete;

    ColliderShapeDef(const SphereDef& sphereDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Sphere),
          m_Sphere(sphereDef)
    {
    }

    ColliderShapeDef(const BoxDef& boxDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Box),
          m_Box(boxDef)
    {
    }

    ColliderShapeDef(const CapsuleDef& capsuleDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Capsule),
          m_Capsule(capsuleDef)
    {
    }

    ColliderShapeType GetType() const { return m_Type; }

    const SphereDef& GetSphere() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Sphere, "ColliderShapeDef is not a Sphere");
        return m_Sphere; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const BoxDef& GetBox() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Box, "ColliderShapeDef is not a Box");
        return m_Box; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const CapsuleDef& GetCapsule() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Capsule, "ColliderShapeDef is not a Capsule");
        return m_Capsule; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

private:
    ColliderShapeType m_Type;

    union
    {
        SphereDef m_Sphere;
        BoxDef m_Box;
        CapsuleDef m_Capsule;
    };
};

struct ColliderDef final
{
    ColliderShapeDef Shape;
    CollisionType CollisionType{ CollisionType::Block };
};

struct RigidBodyDef final
{
    Mass Mass;
    MotionType MotionType{ MotionType::Static };
    std::vector<ColliderDef> Colliders;
};

struct ChildNodeDef final
{
    SceneDefs::NameString Name;
    TrsTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
};

struct RootNodeDef final
{
    SceneDefs::NameString Name;
    TrsTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
    std::optional<RigidBodyDef> Body;
};

struct SceneDef final
{
    std::vector<ModelDef> ModelDefs;
    std::vector<RootNodeDef> NodeDefs;
};

/// Runtime representations of level elements.

class Level;

class MeshInstance
{
public:
    MeshInstance() = delete;

    struct Params
    {
        uint32_t IndexCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
        uint32_t FirstInstance;
        uint32_t MaterialIndex;
        BoundingSphere BoundingSphere;
    };

    explicit MeshInstance(const Params& params)
        : m_Params(params)
    {
    }

    uint32_t GetMaterialIndex() const { return m_Params.MaterialIndex; }
    uint32_t GetIndexCount() const { return m_Params.IndexCount; }
    uint32_t GetFirstIndex() const { return m_Params.FirstIndex; }
    uint32_t GetBaseVertex() const { return m_Params.BaseVertex; }
    uint32_t GetFirstInstance() const { return m_Params.FirstInstance; }
    const BoundingSphere& GetBoundingSphere() const { return m_Params.BoundingSphere; }

private:

    Params m_Params;
};

class SceneNode
{
public:
    enum class Flags : uint8_t
    {
        None = 0,
        Active = 1 << 0,
        Visible = 1 << 1,
        All = Active | Visible
    };

    SceneNode(const TrsTransformf& localTransform, const SceneNode* parent)
        : m_LocalTransform(localTransform),
          m_Parent(parent)
    {
    }

    SceneNode() = delete;
    ~SceneNode() = default;
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) = default;
    SceneNode& operator=(SceneNode&&) = default;

    bool IsActive() const { return (m_Flags & Flags::Active) == Flags::Active; }
    bool IsVisible() const { return (m_Flags & Flags::Visible) == Flags::Visible; }

    const TrsTransformf& GetLocalTransform() const { return m_LocalTransform; }
    const Mat44f& GetWorldTransform() const { return m_WorldTransform; }
    const Vec3f& GetLinearVelocity() const { return m_LinearVelocity; }
    const Vec3f& GetAngularVelocity() const { return m_AngularVelocity; }
    const SceneNode* GetParent() const { return m_Parent; }

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
    const SceneNode* m_Parent{ nullptr };
    std::span<SceneNode> m_Children;
    Flags m_Flags{ Flags::Active | Flags::Visible };
};

class PhysicsNode
{
public:
    PhysicsNode(SceneNode& node, const RigidBodyIdentifier rigidBodyId);

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

    SceneNode* m_Node{ nullptr };
    RigidBodyIdentifier m_RigidBodyId;
};

class ModelNode
{
public:
    ModelNode(const SceneNode& node,
        const BoundingSphere& boundingSphere,
        std::span<const MeshInstance> meshInstances);

    ModelNode() = delete;
    ~ModelNode() = default;
    ModelNode(const ModelNode&) = delete;
    ModelNode& operator=(const ModelNode&) = delete;
    ModelNode(ModelNode&&) = default;
    ModelNode& operator=(ModelNode&&) = default;

    const Mat44f& GetWorldTransform() const { return m_Node->GetWorldTransform(); }

    const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

    uint32_t GetMeshCount() const { return static_cast<uint32_t>(m_Meshes.size()); }

    std::span<const MeshInstance> GetMeshes() const { return m_Meshes; }

    bool IsVisible() const { return m_Node->IsVisible(); }

private:
    friend Level;

    const SceneNode* m_Node{ nullptr };
    BoundingSphere m_BoundingSphere;
    std::span<const MeshInstance> m_Meshes;
};