#pragma once

#include "GpuTypes.h"
#include "LevelTypes.h"
#include "Result.h"

#include <filesystem>
#include <vector>

class FileFetcher;
class GpuHelper;
struct PropKitDef;
class ResourceBundle;
class ThreadPool;

class PropKit
{
public:
    static Result<PropKit> Create(GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        const std::filesystem::path& rootPath,
        const ResourceBundle& resourceBundle);

    PropKit() = delete;
    ~PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    const wgpu::BindGroup* GetMaterialBindGroup(const MaterialIdentifier& materialId) const;

    GpuVertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    GpuIndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

private:

    PropKit(GpuVertexBuffer&& vertexBuffer,
        GpuIndexBuffer&& indexBuffer,
        std::vector<wgpu::BindGroup>&& materialBindGroups);

    GpuVertexBuffer m_VertexBuffer;
    GpuIndexBuffer m_IndexBuffer;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
};