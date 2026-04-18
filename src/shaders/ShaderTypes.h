#pragma once

#include "Color.h"
#include "VecMath.h"

using MaterialIndex = uint32_t;
using TransformIndex = uint32_t;
using ModelIndex = uint32_t;

// These types mirror the corresponding types defined in shaders.
namespace ShaderTypes
{
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
        Mat44f WorldTransform;
    };

    class ClipSpaceTransform
    {
    public:
        Mat44f ClipFromWorld;
    };

    class MeshDrawData
    {
    public:
        TransformIndex TransformIndex;
        MaterialIndex MaterialIndex;
    };

    class CameraParams
    {
    public:
        Mat44f View;
        Mat44f Projection;
        Mat44f ViewProj;
    };
}   // namespace ShaderTypes