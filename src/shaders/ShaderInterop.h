#pragma once

#include "Color.h"
#include "VecMath.h"


namespace ShaderInterop
{

static_assert(std::is_floating_point_v<RgbaColorf::ValueType>,
    "RgbaColorf must use a floating-point type");
static_assert(sizeof(RgbaColorf) == 16,
    "RgbaColorf must be 16 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec4f::ValueType>,
    "Vec4f must use a floating-point type");
static_assert(sizeof(Vec4f) == 16,
    "Vec4f must be 16 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec3f::ValueType>,
    "Vec3f must use a floating-point type");
static_assert(sizeof(Vec3f) == 12,
    "Vec3f must be 12 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec2f::ValueType>,
    "Vec2f must use a floating-point type");
static_assert(sizeof(Vec2f) == 8,
    "Vec2f must be 8 bytes to ensure proper alignment in uniform/storage buffers");

// These types mirror the corresponding types defined in shaders.
class DrawIndirectParams
{
public:
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

class MaterialConstants
{
public:
    /// @brief Base color of the material.
    const RgbaColorf Color;

    /// @brief Metalness factor of the material.
    const float Metalness{ 0 };

    /// @brief Roughness factor of the material.
    const float Roughness{ 0 };

    // Align to 16 bytes for storage in a uniform/storage buffer.
    const float pad0;
    const float pad1;
};

class WorldTransform
{
public:
    Mat44f Transform;
};

class ClipSpaceTransform
{
public:
    Mat44f Transform;
};

class MeshProperties
{
public:
    float Radius;
    uint32_t TransformIndex;
    uint32_t MaterialIndex;
};

class CameraParams
{
public:
    Mat44f View;
    Mat44f Projection;
    Mat44f ViewProj;
};
} // namespace ShaderInterop