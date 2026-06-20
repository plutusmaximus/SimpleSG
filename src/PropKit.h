#pragma once

#include "Bounds.h"
#include "LevelDefs.h"
#include "shaders/GpuBufferTypes.h"
#include "shaders/ShaderInterop.h"
#include "StringArena.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

class Mesh
{
public:
    Mesh() = delete;

    struct VertexParams
    {
        uint32_t IndexCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
    };

    Mesh(const VertexParams& vertexParams,
        const MaterialIdentifier materialId,
        const BoundingBox& boundingBox)
        : m_IndexCount(vertexParams.IndexCount),
          m_FirstIndex(vertexParams.FirstIndex),
          m_BaseVertex(vertexParams.BaseVertex),
          m_MaterialId(materialId),
          m_BoundingBox(boundingBox)
    {
    }

    uint32_t GetIndexCount() const { return m_IndexCount; }
    uint32_t GetFirstIndex() const { return m_FirstIndex; }
    uint32_t GetBaseVertex() const { return m_BaseVertex; }
    MaterialIdentifier GetMaterialId() const { return m_MaterialId; }
    const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }

private:

    uint32_t m_IndexCount;
    uint32_t m_FirstIndex;
    uint32_t m_BaseVertex;
    MaterialIdentifier m_MaterialId;
    BoundingBox m_BoundingBox;
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

    Result<ModelIdentifier> GetModelId(const std::string_view& name) const
    {
        auto it = m_ModelNameToId.find(name);
        MLG_CHECKV(it != m_ModelNameToId.end(), "Model not found: {}", name);

        return it->second;
    }

    Result<std::span<const Mesh>> GetMeshes(const ModelIdentifier& modelId) const;

    Result<BoundingSphere> GetBoundingSphere(const ModelIdentifier& modelId) const;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    MaterialConstantsBuffer GetMaterialConstantsBuffer() const { return m_MaterialConstantsBuffer; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:
    class Model
    {
    public:
        Model(const StringHandle& name,
            const MeshIdentifier& firstMeshId,
            const size_t meshCount,
            const BoundingBox& boundingBox,
            const BoundingSphere& boundingSphere)
            : m_Name(name),
              m_FirstMeshId(firstMeshId),
              m_MeshCount(meshCount),
              m_BoundingBox(boundingBox),
              m_BoundingSphere(boundingSphere)
        {
        }

        const StringHandle& GetName() const { return m_Name; }
        const MeshIdentifier& GetFirstMeshId() const { return m_FirstMeshId; }
        size_t GetMeshCount() const { return m_MeshCount; }
        const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }
        const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

    private:
        StringHandle m_Name;
        MeshIdentifier m_FirstMeshId;
        size_t m_MeshCount{ 0 };
        BoundingBox m_BoundingBox;
        BoundingSphere m_BoundingSphere;
    };

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
    MaterialConstantsBuffer m_MaterialConstantsBuffer;
    std::unordered_map<std::string_view, ModelIdentifier> m_ModelNameToId;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    StringArena m_StringArena;
};