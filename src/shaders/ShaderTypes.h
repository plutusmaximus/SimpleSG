#pragma once

#include "Color.h"
#include "VecMath.h"

#include <limits>

using MaterialIndex = uint32_t;
using NodeIndex = uint32_t;
using ModelIndex = uint32_t;

static constexpr MaterialIndex kInvalidMaterialIndex = std::numeric_limits<MaterialIndex>::max();
static constexpr ModelIndex kInvalidModelIndex = std::numeric_limits<ModelIndex>::max();
static constexpr NodeIndex kInvalidNodeIndex = std::numeric_limits<NodeIndex>::max();

// These types mirror the corresponding types defined in shaders.
namespace ShaderTypes
{

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

class MeshTransform
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
    Vec3f Center;
    float Radius;
    NodeIndex NodeIndex;
    MaterialIndex MaterialIndex;
    uint32_t pad0;
    uint32_t pad1;
};

class CameraParams
{
public:
    Mat44f View;
    Mat44f Projection;
    Mat44f ViewProj;
};
} // namespace ShaderTypes