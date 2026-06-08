#pragma once

#include "Bounds.h"
#include "LevelDefs.h"
#include "ShaderInterop.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

// Strongly-typed GPU storage buffer classes.
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants>;

struct Mesh final
{
    uint32_t IndexCount{ 0 };
    uint32_t FirstIndex{ 0 };
    uint32_t BaseVertex{ 0 };
    MaterialIdentifier MaterialId;
    Box BoundingBox;
};

class PropKit
{
public:
    static Result<PropKit> Create(const std::filesystem::path& rootPath,
        class TextureCache& textureCache,
        const PropKitDef& propKitDef);

    PropKit() = delete;
    ~PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    Result<ModelIdentifier> GetModeld(const std::string_view& name) const
    {
        auto it = m_ModelNameToId.find(name);
        MLG_CHECKV(it != m_ModelNameToId.end(), "Model not found: {}", name);

        return it->second;
    }

    Result<std::span<const Mesh>> GetMeshes(const ModelIdentifier& modelId) const;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    MaterialConstantsBuffer GetMaterialConstantsBuffer() const { return m_MaterialConstantsBuffer; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    struct Model
    {
        std::string_view Name;
        MeshIdentifier FirstMeshId;
        size_t MeshCount{ 0 };
    };

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
    std::unordered_map<std::string_view, ModelIdentifier> m_ModelNameToId;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    // Storage for model names to ensure they remain valid for string_views
    // and to reduce memory fragmentation by storing all names contiguously.
    std::vector<char> m_StringStorage;
};