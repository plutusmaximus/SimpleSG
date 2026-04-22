#pragma once

#include "Bounds.h"
#include "Result.h"
#include "TextureCache.h"
#include "VecMath.h"

#include <filesystem>
#include <span>
#include <vector>

struct Mesh
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

class PropKit
{
public:
    static Result<> Create(const std::filesystem::path& rootPath,
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
        std::vector<Mesh>&& meshes,
        std::vector<Model>&& models,
        MaterialConstantsBuffer materialConstantsBuffer,
        std::vector<wgpu::BindGroup>&& materialBindGroups)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_Meshes(std::move(meshes)),
          m_Models(std::move(models)),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_MaterialBindGroups(std::move(materialBindGroups))
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

    uint32_t GetMeshCount() const { return static_cast<uint32_t>(m_Meshes.size()); }

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    MaterialConstantsBuffer GetMaterialConstantsBuffer() const { return m_MaterialConstantsBuffer; }

    const std::span<const Mesh> GetMeshes() const { return m_Meshes; }

    const std::span<const Model> GetModels() const { return m_Models; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    MaterialConstantsBuffer m_MaterialConstantsBuffer;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
};