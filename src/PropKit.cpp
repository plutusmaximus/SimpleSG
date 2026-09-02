#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PROP"

#include "PropKit.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "LevelDefs.h"
#include "Log.h"
#include "TextureCache.h"
#include "TextureFetcher.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <filesystem>
#include <map>
#include <ranges>
#include <stb_image.h>

namespace
{

Result<>
FetchTextures(GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& basePath,
    const std::span<std::string_view> textureUris,
    TextureCache& textureCache)
{
    TextureFetcher fetcher(gpuHelper, threadPool, fileFetcher, textureCache, basePath, textureUris);

    while(!fetcher.IsComplete())
    {
        fetcher.Update();
    }

    MLG_CHECK(fetcher.Succeeded(), "Failed to fetch textures");

    return Result<>::Ok;
}

Result<std::vector<wgpu::BindGroup>>
CreateMaterialBindGroups(GpuHelper& gpuHelper,
    const std::span<const MaterialDef> materialDefs,
    const TextureCache& textureCache)
{
    std::vector<wgpu::BindGroup> materialBindGroups;
    materialBindGroups.reserve(materialDefs.size());

    for(const auto& mtlDef : materialDefs)
    {
        const wgpu::Texture& baseTexture = mtlDef.BaseTextureUri.empty()
            ? gpuHelper.GetDefaultTexture()
            : textureCache.Get(mtlDef.BaseTextureUri);

        const ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlDef.Color,
                .Metalness = mtlDef.Metalness,
                .Roughness = mtlDef.Roughness,
            };

        auto buffer =
            gpuHelper.CreateUniformBuffer<GpuMaterialConstantsBuffer>(1, "MaterialConstants");
        MLG_CHECK(buffer);

        buffer->Store(0, mc);

        auto bindGroup =
            gpuHelper.CreateMaterialBindGroup(baseTexture, *buffer, mtlDef.BaseTextureUri);
        MLG_CHECK(bindGroup);

        materialBindGroups.push_back(std::move(*bindGroup));
    }

    return materialBindGroups;
}

std::map<MaterialDef, MaterialIdentifier>
BuildUniqueMaterialsMap(const std::span<const ModelDef> modelDefs)
{
    std::map<MaterialDef, MaterialIdentifier> uniqueMaterialMap;
    size_t materialIndex = 0;

    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            const MaterialDef& materialDef = meshDef.MaterialDef;
            if(!uniqueMaterialMap.contains(materialDef))
            {
                uniqueMaterialMap[materialDef] = MaterialIdentifier(materialIndex++);
            }
        }
    }
    return uniqueMaterialMap;
}

std::vector<Vertex>
BuildVertices(const std::span<const ModelDef> modelDefs)
{
    size_t vertexCount = 0;
    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            vertexCount += meshDef.Vertices.size();
        }
    }
    std::vector<Vertex> vertices;
    vertices.reserve(vertexCount);

    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            vertices.append_range(meshDef.Vertices);
        }
    }

    return vertices;
}

std::vector<VertexIndex>
BuildIndices(const std::span<const ModelDef> modelDefs)
{
    size_t indexCount = 0;
    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            indexCount += meshDef.Indices.size();
        }
    }

    std::vector<VertexIndex> indices;
    indices.reserve(indexCount);

    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            indices.append_range(meshDef.Indices);
        }
    }

    return indices;
}
} // namespace

Result<PropKit>
PropKit::Create(GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& rootPath,
    const PropKitDef& propKitDef)
{
    Timer createTimer;
    createTimer.Start();

    const std::map<MaterialDef, MaterialIdentifier> uniqueMaterialMap =
        BuildUniqueMaterialsMap(propKitDef.ModelDefs);

    std::vector<MaterialDef> uniqueMaterials;
    std::vector<std::string_view> textureUris;
    uniqueMaterials.resize(uniqueMaterialMap.size());
    textureUris.resize(uniqueMaterialMap.size());
    for(const auto& [materialDef, id] : uniqueMaterialMap)
    {
        uniqueMaterials[id.GetValue()] = materialDef;
        textureUris[id.GetValue()] = materialDef.BaseTextureUri;
    }

    std::vector<Vertex> vertices = BuildVertices(propKitDef.ModelDefs);
    std::vector<VertexIndex> indices = BuildIndices(propKitDef.ModelDefs);

    TextureCache textureCache(gpuHelper.GetDefaultTexture());

    MLG_CHECK(
        FetchTextures(gpuHelper, threadPool, fileFetcher, rootPath, textureUris, textureCache));

    auto vertexBuffer = gpuHelper.CreateVertexBuffer(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);

    vertexBuffer->Store(vertices);

    auto indexBuffer = gpuHelper.CreateIndexBuffer(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);

    indexBuffer->Store(indices);

    auto materialBindGroups = CreateMaterialBindGroups(gpuHelper, uniqueMaterials, textureCache);
    MLG_CHECK(materialBindGroups);

    PropKit propKit(std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(*materialBindGroups));
    MLG_INFO("PropKit created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(propKit);
}

const wgpu::BindGroup*
PropKit::GetMaterialBindGroup(const MaterialIdentifier& materialId) const
{
    if(MLG_VERIFY(materialId.IsValid() && materialId.GetValue() < m_MaterialBindGroups.size(),
           "Invalid material id: {}",
           materialId.GetValue()))
    {
        return &m_MaterialBindGroups[materialId.GetValue()];
    }

    return nullptr;
}

// private:

PropKit::PropKit(GpuVertexBuffer&& vertexBuffer,
    GpuIndexBuffer&& indexBuffer,
    std::vector<wgpu::BindGroup>&& materialBindGroups)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_MaterialBindGroups(std::move(materialBindGroups))
{
}