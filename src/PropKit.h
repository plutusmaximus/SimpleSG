#pragma once

#include "Bounds.h"
#include "Result.h"
#include "TextureCache.h"
#include "VecMath.h"

#include <filesystem>
#include <span>
#include <vector>

struct MeshProperties
{
    uint32_t IndexCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    MaterialIndex MaterialIndex;
    AABoundingBox BoundingBox;
};

struct Model
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
};

struct MaterialDef
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
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

class PropKitDef
{
public:
    PropKitDef() = default;
    PropKitDef(const PropKitDef&) = delete;
    PropKitDef& operator=(const PropKitDef&) = delete;
    PropKitDef(PropKitDef&&) = default;
    PropKitDef& operator=(PropKitDef&&) = default;

    PropKitDef(std::vector<ModelDef>&& modelDefs,
        std::unordered_map<std::string, uint32_t>&& modelMap)
        : m_ModelDefs(std::move(modelDefs)),
          m_ModelMap(std::move(modelMap))
    {
    }

    std::span<const ModelDef> GetModelDefs() const { return m_ModelDefs; }
    const std::unordered_map<std::string, uint32_t>& GetModelMap() const { return m_ModelMap; }

private:

    std::vector<ModelDef> m_ModelDefs;
    std::unordered_map<std::string, uint32_t> m_ModelMap;
};

// Strongly-typed GPU storage buffer classes.
using MeshPropertiesBuffer = TypedGpuBuffer<ShaderTypes::MeshProperties>;
using MaterialConstantsBuffer = TypedGpuBuffer<ShaderTypes::MaterialConstants>;

struct SceneDef;
class Scene;

class PropKit
{
public:

    static Result<> Load(const std::filesystem::path& rootPath,
        TextureCache& textureCache,
        const PropKitDef& propKitDef,
        const SceneDef& sceneDef,
        PropKit& outPropKit,
        Scene& outScene);

    PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        MaterialConstantsBuffer materialConstantsBuffer,
        IndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<MeshProperties>&& meshProperties,
        std::vector<Model>&& models)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshPropertiesBuffer(meshPropertiesBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_MaterialBindGroups(std::move(materialBindGroups)),
          m_MeshProperties(std::move(meshProperties)),
          m_Models(std::move(models))
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

    IndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }
    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }
    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }

private:

    VertexBuffer m_VertexBuffer{nullptr};
    IndexBuffer m_IndexBuffer{nullptr};
    IndirectBuffer m_DrawIndirectBuffer{nullptr};
    MeshPropertiesBuffer m_MeshPropertiesBuffer{nullptr};
    MaterialConstantsBuffer m_MaterialConstantsBuffer{nullptr};
    wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<MeshProperties> m_MeshProperties;
    std::vector<Model> m_Models;
};