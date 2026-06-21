#pragma once

#include "SceneTypes.h"
#include "shaders/GpuBufferTypes.h"
#include "shaders/ShaderInterop.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

struct PropKitDef;

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

    Result<ModelIdentifier> GetModelId(const std::string_view& name) const
    {
        auto it = m_ModelNameToId.find(name);
        MLG_CHECKV(it != m_ModelNameToId.end(), "Model not found: {}", name);

        return it->second;
    }

    const Model* GetModel(const ModelIdentifier& modelId) const;

    Result<std::span<const Mesh>> GetMeshes(const ModelIdentifier& modelId) const;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    MaterialConstantsBuffer GetMaterialConstants() const { return m_MaterialConstants; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        std::vector<Mesh> meshes,
        std::vector<Model> models,
        MaterialConstantsBuffer materialConstantsBuffer,
        std::vector<wgpu::BindGroup> materialBindGroups,
        StringArena stringArena);

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    MaterialConstantsBuffer m_MaterialConstants;
    std::unordered_map<std::string_view, ModelIdentifier> m_ModelNameToId;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    StringArena m_StringArena;
};