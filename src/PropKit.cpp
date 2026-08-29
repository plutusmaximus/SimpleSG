#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PROP"

#include "PropKit.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "LevelDefs.h"
#include "Log.h"
#include "narrow_cast.h"
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
    TextureFetcher fetcher(gpuHelper,
        threadPool,
        fileFetcher,
        textureCache,
        basePath,
        textureUris);

    while(!fetcher.IsComplete())
    {
        fetcher.Update();
    }

    MLG_CHECK(fetcher.Succeeded(), "Failed to fetch textures");

    return Result<>::Ok;
}

Result<>
CreateMaterialBindGroups(GpuHelper& gpuHelper,
    const std::span<const MaterialDef> materialDefs,
    const TextureCache& textureCache,
    std::vector<wgpu::BindGroup>& materialBindGroups)
{
    materialBindGroups.clear();

    materialBindGroups.reserve(materialDefs.size());

    for(const auto& mtlDef : materialDefs)
    {
        const wgpu::Texture& baseTexture = mtlDef.BaseTextureUri.empty()
            ? gpuHelper.GetDefaultTexture()
            : textureCache.Get(mtlDef.BaseTextureUri);

        auto bindGroup = gpuHelper.CreateTextureBindGroup(baseTexture, mtlDef.BaseTextureUri);
        MLG_CHECK(bindGroup);

        materialBindGroups.push_back(std::move(*bindGroup));
    }

    return Result<>::Ok;
}

Result<GpuMaterialConstantsBuffer>
BuildMaterialConstantsBuffer(GpuHelper& gpuHelper, const std::span<const MaterialDef> materialDefs)
{
    std::vector<ShaderInterop::MaterialConstants> materialConstants;
    materialConstants.reserve(materialDefs.size());

    for(const auto& mtlDef : materialDefs)
    {
        const ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlDef.Color,
                .Metalness = mtlDef.Metalness,
                .Roughness = mtlDef.Roughness,
            };

        materialConstants.push_back(mc);
    }

    auto buffer =
        gpuHelper.CreateStorageBuffer<GpuMaterialConstantsBuffer>(materialConstants.size(),
            "MaterialConstants");

    MLG_CHECK(buffer);

    buffer->Store(materialConstants);

    return buffer;
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

Result<std::vector<Mesh>>
BuildMeshes(const std::span<const ModelDef> modelDefs,
    const std::map<MaterialDef, MaterialIdentifier>& uniqueMaterialMap)
{
    size_t meshCount = 0;
    for(const auto& modelDef : modelDefs)
    {
        meshCount += modelDef.MeshDefs.size();
    }

    std::vector<Mesh> meshes;
    meshes.reserve(meshCount);

    size_t firstIndex = 0;
    size_t baseVertex = 0;

    for(const auto& modelDef : modelDefs)
    {
        for(const auto& meshDef : modelDef.MeshDefs)
        {
            auto it = uniqueMaterialMap.find(meshDef.MaterialDef);
            MLG_CHECKV(it != uniqueMaterialMap.end(),
                "Failed to find material for mesh");
            const MaterialIdentifier materialId = it->second;

            const Mesh::VertexParams vertexParams //
                {
                    .IndexCount = narrow_cast<uint32_t>(meshDef.Indices.size()),
                    .FirstIndex = narrow_cast<uint32_t>(firstIndex),
                    .BaseVertex = narrow_cast<uint32_t>(baseVertex),
                };

            const BoundingBox aabb = BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices);

            meshes.emplace_back(vertexParams, materialId, aabb);
            firstIndex += meshDef.Indices.size();
            baseVertex += meshDef.Vertices.size();
        }
    }

    return meshes;
}

std::vector<Model>
BuildModels(const std::span<const ModelDef> modelDefs, const std::vector<Mesh>& meshes)
{
    std::vector<Model> models;
    models.reserve(modelDefs.size());

    size_t meshIndex = 0;

    for(const auto& modelDef : modelDefs)
    {
        const std::span meshSpan = std::span(meshes).subspan(meshIndex, modelDef.MeshDefs.size());
        BoundingBox aabb = meshSpan.front().GetBoundingBox();
        for(const Mesh& mesh : meshSpan.subspan(1))
        {
            aabb += mesh.GetBoundingBox();
        }
        models.emplace_back(meshSpan, aabb);
        meshIndex += modelDef.MeshDefs.size();
    }

    return models;
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

    auto meshes = BuildMeshes(propKitDef.ModelDefs, uniqueMaterialMap);
    MLG_CHECK(meshes);
    std::vector<Model> models = BuildModels(propKitDef.ModelDefs, *meshes);
    std::vector<Vertex> vertices = BuildVertices(propKitDef.ModelDefs);
    std::vector<VertexIndex> indices = BuildIndices(propKitDef.ModelDefs);

    size_t totalStringSize = 0;
    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        totalStringSize += modelDef.Name.size() + 1;
    }
    StringArena stringArena(totalStringSize);

    std::vector<NameIndexPair> modelNameIndex;
    modelNameIndex.reserve(propKitDef.ModelDefs.size());

    for(size_t i = 0; i < propKitDef.ModelDefs.size(); ++i)
    {
        const auto& modelDef = propKitDef.ModelDefs[i];
        const StringHandle modelName = stringArena.NewString(modelDef.Name);

        modelNameIndex.emplace_back(modelName, i);
    }

    TextureCache textureCache(gpuHelper.GetDefaultTexture());

    MLG_CHECK(
        FetchTextures(gpuHelper, threadPool, fileFetcher, rootPath, textureUris, textureCache));

    auto vertexBuffer = gpuHelper.CreateVertexBuffer(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);

    vertexBuffer->Store(vertices);

    auto indexBuffer = gpuHelper.CreateIndexBuffer(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);

    indexBuffer->Store(indices);

    auto materialConstants = BuildMaterialConstantsBuffer(gpuHelper, uniqueMaterials);
    MLG_CHECK(materialConstants);

    std::vector<wgpu::BindGroup> materialBindGroups;
    MLG_CHECK(
        CreateMaterialBindGroups(gpuHelper, uniqueMaterials, textureCache, materialBindGroups));

    PropKit propKit(std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(*materialConstants),
        std::move(materialBindGroups),
        std::move(*meshes),
        std::move(models),
        std::move(modelNameIndex),
        std::move(stringArena));
    MLG_INFO("PropKit created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(propKit);
}

const Model*
PropKit::GetModel(const std::string_view& name) const
{
    auto it = std::ranges::lower_bound(m_ModelNameIndex, name, {}, &NameIndexPair::Name);

    if(!MLG_VERIFY(m_ModelNameIndex.end() != it && it->Name == name, "Model not found: {}", name))
    {
        return nullptr;
    }

    return &m_Models[it->Index];
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
    GpuMaterialConstantsBuffer&& materialConstants,
    std::vector<wgpu::BindGroup>&& materialBindGroups,
    std::vector<Mesh>&& meshes,
    std::vector<Model>&& models,
    std::vector<NameIndexPair>&& modelNameIndex,
    StringArena&& stringArena)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_MaterialConstants(std::move(materialConstants)),
      m_MaterialBindGroups(std::move(materialBindGroups)),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_ModelNameIndex(std::move(modelNameIndex)),
      m_StringArena(std::move(stringArena))
{
    std::ranges::sort(m_ModelNameIndex, {}, &NameIndexPair::Name);

    for(size_t i = 1; i < m_ModelNameIndex.size(); ++i)
    {
        const StringHandle& a = m_ModelNameIndex[i - 1].Name;
        const StringHandle& b = m_ModelNameIndex[i].Name;

        if(!MLG_VERIFY(a != b, "Duplicate model name found: {}", a))
        {
            MLG_ERROR("Duplicate model name found: {}", a);
        }
    }
}