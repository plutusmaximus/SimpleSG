#include <string>
#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PROP"

#include "PropKit.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "Log.h"
#include "ResourceBundle.h"
#include "TextureCache.h"
#include "TextureFetcher.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <filesystem>
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
    const std::span<const MaterialResource> materialRsrcs,
    const std::span<const wgpu::Texture> textures,
    const std::span<const std::string_view> textureUris)
{
    std::vector<wgpu::BindGroup> materialBindGroups;
    materialBindGroups.reserve(materialRsrcs.size());

    for(const MaterialResource& mtlRsrc : materialRsrcs)
    {
        MLG_CHECKV(mtlRsrc.BaseTextureIndex == Resource::kInvalidIndex
                || mtlRsrc.BaseTextureIndex < textures.size(),
            "Invalid base texture index");

        wgpu::Texture baseTexture;
        std::string_view textureUri;
        if(mtlRsrc.BaseTextureIndex == Resource::kInvalidIndex)
        {
            baseTexture = gpuHelper.GetDefaultTexture();
            textureUri = "<default>";
        }
        else
        {
            baseTexture = textures[mtlRsrc.BaseTextureIndex];
            textureUri = textureUris[mtlRsrc.BaseTextureIndex];
        }

        const ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlRsrc.Color,
                .Metalness = mtlRsrc.Metalness,
                .Roughness = mtlRsrc.Roughness,
            };

        auto buffer =
            gpuHelper.CreateUniformBuffer<GpuMaterialConstantsBuffer>(1, "MaterialConstants");
        MLG_CHECK(buffer);

        buffer->Store(0, mc);

        auto bindGroup =
            gpuHelper.CreateMaterialBindGroup(baseTexture, *buffer, textureUri);
        MLG_CHECK(bindGroup);

        materialBindGroups.push_back(std::move(*bindGroup));
    }

    return materialBindGroups;
}
} // namespace

Result<PropKit>
PropKit::Create(GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& rootPath,
    const ResourceBundle& resourceBundle)
{
    Timer createTimer;
    createTimer.Start();

    TextureCache textureCache(gpuHelper.GetDefaultTexture());

    const std::span textureUriStrings = resourceBundle.GetTextureUris();
    std::vector<std::string_view> textureUris;
    textureUris.reserve(textureUriStrings.size());
    for(const auto& uri : textureUriStrings)
    {
        textureUris.push_back(resourceBundle.GetString(uri));
    }

    MLG_CHECK(
        FetchTextures(gpuHelper, threadPool, fileFetcher, rootPath, textureUris, textureCache));

    std::vector<wgpu::Texture> textures;
    textures.reserve(textureUris.size());
    for(const std::string_view& uri : textureUris)
    {
        textures.push_back(textureCache.Get(uri));
    }

    const std::span vertices = resourceBundle.GetVertices();
    auto vertexBuffer = gpuHelper.CreateVertexBuffer(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);

    vertexBuffer->Store(vertices);

    const std::span indices = resourceBundle.GetIndices();
    auto indexBuffer = gpuHelper.CreateIndexBuffer(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);

    indexBuffer->Store(indices);

    auto materialBindGroups =
        CreateMaterialBindGroups(gpuHelper, resourceBundle.GetMaterials(), textures, textureUris);
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