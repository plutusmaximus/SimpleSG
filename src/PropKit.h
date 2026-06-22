#pragma once

#include "SceneTypes.h"
#include "shaders/GpuBufferTypes.h"
#include "shaders/ShaderInterop.h"

#include <filesystem>
#include <span>
#include <vector>

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

    const Model* GetModel(const std::string_view& name) const;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    MaterialConstantsBuffer GetMaterialConstants() const { return m_MaterialConstants; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        MaterialConstantsBuffer materialConstants,
        std::vector<wgpu::BindGroup> materialBindGroups,
        std::vector<Mesh> meshes,
        std::vector<Model> models,
        StringArena stringArena);

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    MaterialConstantsBuffer m_MaterialConstants;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;

    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    StringArena m_StringArena;
};