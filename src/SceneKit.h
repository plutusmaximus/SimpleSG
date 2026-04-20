#pragma once

#include "Bounds.h"
#include "Result.h"
#include "SceneKit.h"
#include "shaders/ShaderTypes.h"
#include "TextureCache.h"
#include "VecMath.h"

#include <filesystem>
#include <span>
#include <vector>

struct MeshProperties
{
    MaterialIndex MaterialIndex;
    AABoundingBox BoundingBox;
};

struct MeshData
{
    uint32_t FirstIndex;
    uint32_t IndexCount;
    uint32_t BaseVertex;
    MeshProperties Properties;
};

struct ModelInstance
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
    TransformIndex TransformIndex;
};

struct MaterialData
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct TransformData
{
    static constexpr TransformIndex kInvalidParentIndex = std::numeric_limits<TransformIndex>::max();

    Mat44f Transform;
    TransformIndex ParentIndex{ kInvalidParentIndex };
};

class SceneKitSourceData
{
public:

    SceneKitSourceData() = default;
    SceneKitSourceData(const SceneKitSourceData&) = delete;
    SceneKitSourceData& operator=(const SceneKitSourceData&) = delete;
    SceneKitSourceData(SceneKitSourceData&&) = default;
    SceneKitSourceData& operator=(SceneKitSourceData&&) = default;

    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    std::vector<MaterialData> Materials;
    std::vector<TransformData> Transforms;
    std::vector<MeshData> Meshes;
    std::vector<ModelInstance> ModelInstances;
};

// Strongly-typed GPU storage buffer classes.
using TransformBuffer = TypedGpuBuffer<ShaderTypes::MeshTransform>;
using MeshDrawDataBuffer = TypedGpuBuffer<ShaderTypes::MeshDrawData>;
using MaterialConstantsBuffer = TypedGpuBuffer<ShaderTypes::MaterialConstants>;

class SceneKit
{
public:

    static Result<> Load(const std::filesystem::path& rootPath,
        TextureCache& textureCache,
        const SceneKitSourceData& sceneKitData,
        SceneKit& outSceneKit);

    SceneKit() = default;
    SceneKit(const SceneKit&) = delete;
    SceneKit& operator=(const SceneKit&) = delete;
    SceneKit(SceneKit&& other) = default;
    SceneKit& operator=(SceneKit&& other) = default;

    SceneKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        TransformBuffer transformBuffer,
        MaterialConstantsBuffer materialConstantsBuffer,
        IndirectBuffer drawIndirectBuffer,
        MeshDrawDataBuffer meshDrawDataBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<MeshProperties>&& meshes,
        std::vector<ModelInstance>&& modelInstances)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshDrawDataBuffer(meshDrawDataBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
          m_MaterialBindGroups(std::move(materialBindGroups)),
          m_Meshes(std::move(meshes)),
          m_ModelInstances(std::move(modelInstances))
    {
#ifndef NDEBUG
        for(const auto& mesh : m_Meshes)
        {
            const Vec3f& aabbMax = mesh.BoundingBox.GetMax();
            const Vec3f& aabbMin = mesh.BoundingBox.GetMin();
            MLG_ASSERT(aabbMin != aabbMax, "Mesh has degenerate bounding box");
            MLG_ASSERT(aabbMin.x <= aabbMax.x &&
                        aabbMin.y <= aabbMax.y &&
                        aabbMin.z <= aabbMax.z,
                "Mesh has invalid bounding box");
        }
#endif // NDEBUG
    }

    uint32_t GetTransformCount() const
    {
        if (m_TransformBuffer)
        {
            return static_cast<uint32_t>(m_TransformBuffer.GetSize() / sizeof(Mat44f));
        }
        return 0;
    }

    uint32_t GetMeshCount() const
    {
        return static_cast<uint32_t>(m_Meshes.size());
    }

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    const std::span<const MeshProperties> GetMeshes() const
    {
        return m_Meshes;
    }

    const std::span<const ModelInstance> GetModelInstances() const
    {
        return m_ModelInstances;
    }

    TransformBuffer GetTransformBuffer() const { return m_TransformBuffer; }
    IndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }
    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }
    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }
    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:

    VertexBuffer m_VertexBuffer{nullptr};
    IndexBuffer m_IndexBuffer{nullptr};
    TransformBuffer m_TransformBuffer{nullptr};
    IndirectBuffer m_DrawIndirectBuffer{nullptr};
    MeshDrawDataBuffer m_MeshDrawDataBuffer{nullptr};
    MaterialConstantsBuffer m_MaterialConstantsBuffer{nullptr};
    wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
    wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<MeshProperties> m_Meshes;
    std::vector<ModelInstance> m_ModelInstances;
};