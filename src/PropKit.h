#pragma once

#include "Bounds.h"
#include "Result.h"
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

struct Model
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
};

struct ModelInstance
{
    ModelIndex ModelIndex;
    TransformIndex TransformIndex;
};

struct MaterialDef
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct TransformDef
{
    static constexpr TransformIndex kInvalidParentIndex = std::numeric_limits<TransformIndex>::max();

    Mat44f Transform;
    TransformIndex ParentIndex{ kInvalidParentIndex };
};

struct MeshDef
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    MaterialDef MaterialDef;
};

struct ModelDef
{
    std::vector<MeshDef> MeshDefs;
};

struct PropKitDef
{
    std::vector<ModelDef> ModelDefs;
    std::vector<TransformDef> TransformDefs;
    std::vector<ModelInstance> ModelInstances;
};

// Strongly-typed GPU storage buffer classes.
using TransformBuffer = TypedGpuBuffer<ShaderTypes::MeshTransform>;
using MeshPropertiesBuffer = TypedGpuBuffer<ShaderTypes::MeshProperties>;
using MaterialConstantsBuffer = TypedGpuBuffer<ShaderTypes::MaterialConstants>;

class PropKit
{
public:

    static Result<> Load(const std::filesystem::path& rootPath,
        TextureCache& textureCache,
        const PropKitDef& propKitDef,
        PropKit& outPropKit);

    PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        TransformBuffer transformBuffer,
        MaterialConstantsBuffer materialConstantsBuffer,
        IndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<MeshProperties>&& meshProperties,
        std::vector<Model>&& models,
        std::vector<ModelInstance>&& modelInstances)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshPropertiesBuffer(meshPropertiesBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
          m_MaterialBindGroups(std::move(materialBindGroups)),
          m_MeshProperties(std::move(meshProperties)),
          m_Models(std::move(models)),
          m_ModelInstances(std::move(modelInstances))
    {
#ifndef NDEBUG
        for(const auto& meshProps : m_MeshProperties)
        {
            const Vec3f& aabbMax = meshProps.BoundingBox.GetMax();
            const Vec3f& aabbMin = meshProps.BoundingBox.GetMin();
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
        return static_cast<uint32_t>(m_MeshProperties.size());
    }

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    const std::span<const MeshProperties> GetMeshProperties() const
    {
        return m_MeshProperties;
    }

    const std::span<const Model> GetModels() const
    {
        return m_Models;
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
    MeshPropertiesBuffer m_MeshPropertiesBuffer{nullptr};
    MaterialConstantsBuffer m_MaterialConstantsBuffer{nullptr};
    wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
    wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<MeshProperties> m_MeshProperties;
    std::vector<Model> m_Models;
    std::vector<ModelInstance> m_ModelInstances;
};