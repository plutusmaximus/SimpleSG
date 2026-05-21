#pragma once

#include "Color.h"
#include "VecMath.h"


namespace ShaderInterop
{

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