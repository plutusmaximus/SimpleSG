#pragma once

#include "Result.h"
#include "Color.h"
#include "SceneKit.h"
#include "VecMath.h"

#include <limits>
#include <string>
#include <vector>

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
    uint32_t BaseVertex;
    uint32_t IndexCount;
};

struct ModelData
{
    std::vector<MeshData> Meshes;
};

struct ModelInstance
{
    uint32_t ModelIndex;
    uint32_t TransformIndex;
};

struct TransformData
{
    static constexpr uint32_t kInvalidParentIndex = std::numeric_limits<uint32_t>::max();

    Mat44f Transform;
    uint32_t ParentIndex{ kInvalidParentIndex };
};

class CgltfModelLoader final
{
public:

    static Result<SceneKit*> LoadSceneKit(wgpu::Device& device, const std::string& path);
};