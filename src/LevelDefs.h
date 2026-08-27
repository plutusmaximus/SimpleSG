#pragma once

#include "Color.h"
#include "PhysicsTypes.h"
#include "Vertex.h"

#include <optional>
#include <string>
#include <vector>

struct MaterialDef final
{
    std::string BaseTextureUri;
    RgbaColorf Color{ 1, 1, 1, 1 };
    float Metalness{ 0.0f };
    float Roughness{ 0.0f };

    // Used to deduplicate materials based on their properties.
    friend auto operator<=>(const MaterialDef& lhs, const MaterialDef& rhs)
    {
        if(auto cmp = lhs.BaseTextureUri <=> rhs.BaseTextureUri; cmp != 0)
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
    std::string Name;
    std::vector<MeshDef> MeshDefs;
};

struct PropKitDef final
{
    std::vector<ModelDef> ModelDefs;
};

struct ModelRef final
{
    std::string Name;
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
    std::string Name;
    TrsTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
};

struct RootNodeDef final
{
    std::string Name;
    TrsTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
    std::optional<RigidBodyDef> Body;
};

struct LevelDef final
{
    std::vector<RootNodeDef> NodeDefs;
};