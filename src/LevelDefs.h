#pragma once

#include "Color.h"
#include "PhysicsTypes.h"
#include "SemanticIdentifier.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

using ModelIdentifier = SemanticIdentifier<struct ModelIdTag>;
using MeshIdentifier = SemanticIdentifier<struct MeshIdTag>;
using MaterialIdentifier = SemanticIdentifier<struct MaterialIdTag>;

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

struct RigidBodyDef final
{
    Mass Mass;
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

    static SphereDef FromBoxDef(const BoxDef& boxDef)
    {
        const float radius = boxDef.HalfExtents.Length();
        return SphereDef{ .Center = boxDef.Center, .Radius = radius };
    }

    static SphereDef FromCapsuleDef(const CapsuleDef& capsuleDef)
    {
        const float radius = capsuleDef.Radius + capsuleDef.HalfHeight;
        return SphereDef{ .Center = capsuleDef.Center, .Radius = radius };
    }
};

using ColliderDef = std::variant<SphereDef, BoxDef, CapsuleDef>;

struct LevelNodeDef final
{
    struct ComponentsDef final
    {
        std::optional<ModelRef> Model;
        std::optional<RigidBodyDef> Body;
        std::optional<ColliderDef> Collider;
    };

    std::string Name;
    TrsTransformf Transform;
    ComponentsDef Components;
    std::vector<LevelNodeDef> Children;
};

struct LevelDef final
{
    std::vector<LevelNodeDef> NodeDefs;
};