#pragma once

#include "Bounds.h"
#include "Result.h"
#include "SemanticInteger.h"
#include "ShaderInterop.h"
#include "VecMath.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

struct MaterialIndexTag {};
struct MeshIndexTag {};
struct ModelIndexTag {};
using MaterialIndex = SemanticInteger<MaterialIndexTag>;
using MeshIndex = SemanticInteger<MeshIndexTag>;
using ModelIndex = SemanticInteger<ModelIndexTag>;

struct MaterialDef
{
    std::string BaseTextureUri;
    RgbaColorf Color{ 1, 1, 1, 1 };
    float Metalness{ 0.0f };
    float Roughness{ 0.0f };

    // Used to deduplicate materials based on their properties.
    friend auto operator<=>(const MaterialDef& lhs, const MaterialDef& rhs)
    {
        if(auto cmp = lhs.BaseTextureUri <=> rhs.BaseTextureUri; cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Color.r, rhs.Color.r); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.g, rhs.Color.g); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.b, rhs.Color.b); cmp != 0)
        {
            return cmp;
        }
        if(auto cmp = std::strong_order(lhs.Color.a, rhs.Color.a); cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Metalness, rhs.Metalness); cmp != 0)
        {
            return cmp;
        }

        if(auto cmp = std::strong_order(lhs.Roughness, rhs.Roughness); cmp != 0)
        {
            return cmp;
        }

        return std::strong_ordering::equal;
    }
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

struct Mesh
{
    uint32_t IndexCount{ 0 };
    uint32_t FirstIndex{ 0 };
    uint32_t BaseVertex{ 0 };
    MaterialIndex MaterialIndex{ MaterialIndex::INVALID };
    Box BoundingBox;
};

struct Model
{
    std::string_view Name;
    MeshIndex FirstMesh{ MeshIndex::INVALID };
    uint32_t MeshCount{ 0 };
};

// Strongly-typed GPU storage buffer classes.
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants>;

class PropKit
{
public:
    static Result<> Create(const std::filesystem::path& rootPath,
        class TextureCache& textureCache,
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

    Result<ModelIndex> GetModelIndex(const std::string_view& name) const
    {
        auto it = m_ModelNameToIndex.find(name);
        MLG_CHECKV(it != m_ModelNameToIndex.end(), "Model not found: {}", name);

        return it->second;
    }

    const std::span<const Mesh> GetMeshes() const { return m_Meshes; }

    const std::span<const Model> GetModels() const { return m_Models; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:
    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        std::vector<Mesh> meshes,
        std::vector<Model> models,
        MaterialConstantsBuffer materialConstantsBuffer,
        std::vector<wgpu::BindGroup> materialBindGroups,
        std::vector<char> stringStorage);

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    MaterialConstantsBuffer m_MaterialConstantsBuffer;
    std::unordered_map<std::string_view, ModelIndex> m_ModelNameToIndex;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    // Storage for model names to ensure they remain valid for string_views
    // and to reduce memory fragmentation by storing all names contiguously.
    std::vector<char> m_StringStorage;
};