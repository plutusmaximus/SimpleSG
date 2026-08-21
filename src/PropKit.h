#pragma once

#include "GpuTypes.h"
#include "Result.h"
#include "SceneTypes.h"
#include "StringArena.h"

#include <filesystem>
#include <vector>

class FileFetcher;
class GpuHelper;
struct PropKitDef;
class ThreadPool;

class PropKit
{
public:
    static Result<PropKit> Create(GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        const std::filesystem::path& rootPath,
        const PropKitDef& propKitDef);

    PropKit() = delete;
    ~PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    const Model* GetModel(const std::string_view& name) const;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    GpuMaterialConstantsBuffer GetMaterialConstants() const { return m_MaterialConstants; }

    GpuVertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    GpuIndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    struct NameIndexPair
    {
        NameIndexPair() = delete;
        NameIndexPair(const StringHandle& name, size_t index)
            : Name(name), Index(index)
        {
        }

        StringHandle Name;
        size_t Index;
    };

    PropKit(GpuVertexBuffer&& vertexBuffer,
        GpuIndexBuffer&& indexBuffer,
        GpuMaterialConstantsBuffer&& materialConstants,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<Mesh>&& meshes,
        std::vector<Model>&& models,
        std::vector<NameIndexPair>&& modelNameIndex,
        StringArena&& stringArena);

    GpuVertexBuffer m_VertexBuffer;
    GpuIndexBuffer m_IndexBuffer;
    GpuMaterialConstantsBuffer m_MaterialConstants;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;

    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    std::vector<NameIndexPair> m_ModelNameIndex;
    StringArena m_StringArena;
};