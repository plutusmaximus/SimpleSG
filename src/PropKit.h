#pragma once

#include "Bounds.h"
#include "Result.h"
#include "TextureCache.h"
#include "VecMath.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

struct MaterialIndexTag {};
using MaterialIndex = SemanticInteger<MaterialIndexTag>;

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
    std::string Name;
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
    std::string Name;
    std::vector<MeshDef> MeshDefs;
};

struct PropKitDef
{
    std::vector<ModelDef> ModelDefs;
};

// Strongly-typed GPU storage buffer classes.
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderTypes::MeshProperties>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderTypes::MaterialConstants>;

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
    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        std::vector<Mesh>&& meshes,
        std::vector<Model>&& models,
        MaterialConstantsBuffer materialConstantsBuffer,
        std::vector<wgpu::BindGroup>&& materialBindGroups);

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    MaterialConstantsBuffer m_MaterialConstantsBuffer;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::unordered_map<std::string, uint32_t> m_ModelNameToIndex;
};