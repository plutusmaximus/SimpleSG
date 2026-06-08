#pragma once

#include "Color.h"
#include "PhysicsTypes.h"

#include <optional>
#include <string>
#include <vector>
#include <variant>

class ModelIdentifier
{
public:
    ModelIdentifier() = default;
    explicit ModelIdentifier(const size_t value)
        : m_Value(value)
    {
        MLG_ASSERT(value != kInvalidValue, "ModelIdentifier cannot be created with invalid value");
    }

    bool IsValid() const { return m_Value != kInvalidValue; }

    size_t GetValue() const
    {
        MLG_ASSERT(IsValid(), "Cannot get value of invalid ModelIdentifier");
        return m_Value;
    }

    auto operator<=>(const ModelIdentifier& other) const = default;

private:

    constexpr static size_t kInvalidValue = static_cast<size_t>(-1);

    size_t m_Value{kInvalidValue};
};

class MeshIdentifier
{
public:
    MeshIdentifier() = default;
    explicit MeshIdentifier(const size_t value)
        : m_Value(value)
    {
        MLG_ASSERT(value != kInvalidValue, "MeshIdentifier cannot be created with invalid value");
    }

    bool IsValid() const { return m_Value != kInvalidValue; }

    size_t GetValue() const
    {
        MLG_ASSERT(IsValid(), "Cannot get value of invalid MeshIdentifier");
        return m_Value;
    }

    auto operator<=>(const MeshIdentifier& other) const = default;

private:

    constexpr static size_t kInvalidValue = static_cast<size_t>(-1);

    size_t m_Value{kInvalidValue};
};

class MaterialIdentifier
{
public:
    MaterialIdentifier() = default;
    explicit MaterialIdentifier(const size_t value)
        : m_Value(value)
    {
        MLG_ASSERT(value != kInvalidValue, "MaterialIdentifier cannot be created with invalid value");
    }

    bool IsValid() const { return m_Value != kInvalidValue; }

    size_t GetValue() const
    {
        MLG_ASSERT(IsValid(), "Cannot get value of invalid MaterialIdentifier");
        return m_Value;
    }

    auto operator<=>(const MaterialIdentifier& other) const = default;

private:

    constexpr static size_t kInvalidValue = static_cast<size_t>(-1);

    size_t m_Value{kInvalidValue};
};

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
    Vec3f LinearVelocity{ 0 };
    Mass Mass;
};

struct SphereDef final
{
    float Radius{ 0 };
};

struct BoxDef final
{
    Vec3f HalfExtents{ 0 };
};

struct CapsuleDef final
{
    float Radius{ 0 };
    float HalfHeight{ 0 };
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