#pragma once

#include "Color.h"
#include "Result.h"
#include "VecMath.h"
#include "SceneKit.h"

#include <limits>
#include <string>
#include <vector>

class SceneKit;

using ModelIndex = uint32_t;

namespace wgpu
{
class Device;
}

struct MaterialData
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct MeshData
{
    uint32_t FirstIndex;
    uint32_t IndexCount;
    uint32_t BaseVertex;
    MaterialIndex MaterialIndex;
};

struct TransformData
{
    static constexpr TransformIndex kInvalidParentIndex = std::numeric_limits<TransformIndex>::max();

    Mat44f Transform;
    TransformIndex ParentIndex{ kInvalidParentIndex };
};

class CgltfModelLoader final
{
public:

    static Result<SceneKit*> LoadSceneKit(wgpu::Device& device, const std::string& path);
};