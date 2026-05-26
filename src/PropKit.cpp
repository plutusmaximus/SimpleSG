#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PPKT"

#include "PropKit.h"
#include "FileFetcher.h"
#include "Log.h"
#include "narrow_cast.h"
#include "Stopwatch.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <atomic>
#include <filesystem>
#include <map>

#include <stb_image.h>

static constexpr uint32_t kNumTextureChannels = 4;

namespace
{
struct TextureBuilder
{
    std::string Uri;
    const FileFetcher::Request* Request{ nullptr };
    Texture Texture;
    std::byte* MappedMemory{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
};
} // namespace

static Result<>
StageTexture(TextureBuilder& builder)
{
    MLG_DEBUG("Staging texture...");

    int width, height, numChannels;

    if(!stbi_info_from_memory(builder.Request->GetData().data(),
           narrow_cast<int>(builder.Request->GetData().size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", builder.Uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto texture = WebgpuHelper::CreateTexture(static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        builder.Uri);

    MLG_CHECK(texture);

    // It appears that mapping/unmapping must be done on the same thread
    // as other wgpu::Device operations.
    auto mapped = texture->MapBytes();
    MLG_CHECK(mapped);

    builder.Texture = *texture;
    builder.MappedMemory = mapped->data();

    auto decode = [](void* userData)
    {
        auto texBuilder = static_cast<TextureBuilder*>(userData);
        MLG_LOG_SCOPE(texBuilder->Uri);

        MLG_DEBUG("Decoding...");

        int imgWidth, imgHeight, imgNumChannels;
        stbi_uc* data = stbi_load_from_memory(texBuilder->Request->GetData().data(),
            narrow_cast<int>(texBuilder->Request->GetData().size()),
            &imgWidth,
            &imgHeight,
            &imgNumChannels,
            kNumTextureChannels);

        if(!data)
        {
            MLG_ERROR("Error decoding - {}", stbi_failure_reason());
        }
        else
        {
            MLG_ASSERT(texBuilder->Texture.GetWidth() == static_cast<uint32_t>(imgWidth) &&
                texBuilder->Texture.GetHeight() == static_cast<uint32_t>(imgHeight),
                "Decoded image dimensions do not match texture dimensions - {}",
                texBuilder->Uri);
            MLG_ASSERT(texBuilder->Texture.GetFormat() == wgpu::TextureFormat::RGBA8Unorm,
                "Texture format does not match expected format - {}",
                texBuilder->Uri);

            std::byte* dst = static_cast<std::byte*>(texBuilder->MappedMemory);
            const std::byte* src = reinterpret_cast<const std::byte*>(data);
            const uint32_t srcRowStride = static_cast<uint32_t>(imgWidth) * kNumTextureChannels;
            const uint32_t dstRowStride = (srcRowStride + 255) & ~255u;
            for(uint32_t y = 0; y < static_cast<uint32_t>(imgHeight);
                ++y, dst += dstRowStride, src += srcRowStride)
            {
                ::memcpy(dst, src, srcRowStride);
            }
        }

        texBuilder->DecodeComplete = true;
    };

    ThreadPool::Enqueue(decode, &builder);

    return Result<>::Ok;
}

static Result<>
FetchTextures(std::filesystem::path basePath,
    std::span<const MaterialDef> materialDefs,
    TextureCache& textureCache,
    wgpu::CommandEncoder encoder)
{
    std::unordered_map<std::string, FileFetcher::Request> fetchRequests;

    for(const auto& mtl : materialDefs)
    {
        if(mtl.BaseTextureUri.empty())
        {
            // No base texture for this material, skip it.
            continue;
        }

        if(textureCache.Contains(mtl.BaseTextureUri))
        {
            // We've already loaded this texture, skip it.
            continue;
        }

        MLG_LOG_SCOPE(mtl.BaseTextureUri);

        // Set the default texture for this URI in the cache so that if the fetch or subsequent
        // staging fails we will have a valid texture to use.
        textureCache.AddOrReplace(mtl.BaseTextureUri, textureCache.GetDefaultTexture());

        auto [it, inserted] =
            fetchRequests.try_emplace(mtl.BaseTextureUri, (basePath / mtl.BaseTextureUri).string());

        if(!inserted)
        {
            // We've already issued a fetch for this texture, skip it.
             continue;
        }

        MLG_DEBUG("Fetching texture...");

        FileFetcher::Request& request = it->second;

        if(!FileFetcher::Fetch(request))
        {
            MLG_WARN("Failed to fetch texture: {}", request.GetFilePath());
        }
    }

    bool pending;

    do
    {
        pending = false;

        FileFetcher::ProcessCompletions();

        for(auto& [uri, request] : fetchRequests)
        {
            if(request.IsPending())
            {
                pending = true;
                break;
            }
        }
    } while(pending);

    MLG_DEBUG("Loaded {} textures", fetchRequests.size());

    size_t builderCount = 0;
    for(auto& [uri, request] : fetchRequests)
    {
        if(request.Succeeded())
        {
            ++builderCount;
        }
    }

    // Must preallocate the vectore because TextureBuilder contains atomics which are not copyable or movable.
    std::vector<TextureBuilder> textureBuilders(builderCount);
    size_t builderIndex = 0;

    for(auto& [uri, request] : fetchRequests)
    {
        if(!request.Succeeded())
        {
            continue;
        }

        MLG_LOG_SCOPE(uri);

        TextureBuilder& builder = textureBuilders[builderIndex++];

        builder.Uri = uri;
        builder.Request = &request;

        auto result = StageTexture(builder);
        if(!result)
        {
            MLG_WARN("Failed to stage texture: {}", builder.Uri);
        }
    }

    do
    {
        pending = false;

        for(auto& builder : textureBuilders)
        {
            MLG_LOG_SCOPE(builder.Uri);

            if(!builder.DecodeComplete)
            {
                pending = true;
                break;
            }
        }
    } while(pending);

    // Unmap textures to flush the data to the GPU before adding them to the cache.
    // It appears that mapping/unmapping must be done on the same thread
    // as other wgpu::Device operations.
    for(auto& builder : textureBuilders)
    {
        MLG_LOG_SCOPE(builder.Uri);

        builder.Texture.Unmap(encoder);
    }

    for(auto& builder : textureBuilders)
    {
        textureCache.AddOrReplace(builder.Uri, builder.Texture);
    }

    return Result<>::Ok;
}

static Result<>
CreateMaterialBindGroups(std::span<const MaterialDef> materialDefs,
    const TextureCache& textureCache,
    std::vector<wgpu::BindGroup>& materialBindGroups)
{
    materialBindGroups.clear();

    materialBindGroups.reserve(materialDefs.size());

    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    for(const auto& mtlDef : materialDefs)
    {
        Texture baseTexture =
            mtlDef.BaseTextureUri.empty()
                ? textureCache.GetDefaultTexture()
                : textureCache.Get(mtlDef.BaseTextureUri);

        wgpu::BindGroupEntry bgEntries[]//
        {
            {
                .binding = 0,
                .textureView = baseTexture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = textureCache.GetDefaultSampler(),
            },
        };

        wgpu::BindGroupDescriptor bindGroupDesc //
        {
            .label = "MaterialBindGroup",
            .layout = (*bgLayouts)[1],
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

        wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bindGroupDesc);

        materialBindGroups.emplace_back(std::move(bindGroup));
    }

    return Result<>::Ok;
}

static Result<MaterialConstantsBuffer>
BuildMaterialConstantsBuffer(std::span<const MaterialDef> materialDefs)
{
    std::vector<ShaderInterop::MaterialConstants> materialConstants;
    materialConstants.reserve(materialDefs.size());

    for(const auto& mtlDef : materialDefs)
    {
        ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlDef.Color,
                .Metalness = mtlDef.Metalness,
                .Roughness = mtlDef.Roughness,
            };

        materialConstants.emplace_back(mc);
    }

    return WebgpuHelper::CreateStorageBuffer<MaterialConstantsBuffer>(materialConstants,
        "MaterialConstantsBuffer");
}

// Used in std::map to deduplicate materials based on their properties.
template<>
struct std::less<MaterialDef>
{
    bool operator()(const MaterialDef& lhs, const MaterialDef& rhs) const
    {
        if(lhs.BaseTextureUri != rhs.BaseTextureUri)
        {
            return lhs.BaseTextureUri < rhs.BaseTextureUri;
        }
        if(lhs.Color != rhs.Color)
        {
            return std::tie(lhs.Color.r, lhs.Color.g, lhs.Color.b, lhs.Color.a) <
                   std::tie(rhs.Color.r, rhs.Color.g, rhs.Color.b, rhs.Color.a);
        }
        if(lhs.Metalness != rhs.Metalness)
        {
            return lhs.Metalness < rhs.Metalness;
        }
        if(lhs.Roughness != rhs.Roughness)
        {
            return lhs.Roughness < rhs.Roughness;
        }
        return false;
    }
};

Result<>
PropKit::Create(const std::filesystem::path& rootPath,
    TextureCache& textureCache,
    const PropKitDef& propKitDef,
    PropKit& outPropKit)
{
    Stopwatch createTimer;
    createTimer.Mark();

    size_t vertexCount = 0, indexCount = 0, meshCount = 0, totalStringSize = 0;
    uint32_t materialIndex = 0;

    std::map<MaterialDef, MaterialIndex, std::less<MaterialDef>> uniqueMaterialMap;

    // Count total vertices, indices, and meshes while also building a map of unique materials to
    // assign indices to them.
    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        totalStringSize += modelDef.Name.size() + 1;

        for(const auto& mesh : modelDef.MeshDefs)
        {
            const MaterialDef& materialDef = mesh.MaterialDef;
            if(!uniqueMaterialMap.contains(materialDef))
            {
                uniqueMaterialMap[materialDef] = MaterialIndex(materialIndex++);
            }

            vertexCount += mesh.Vertices.size();
            indexCount += mesh.Indices.size();
            meshCount += 1;
        }
    }

    std::vector<MaterialDef> uniqueMaterials;
    uniqueMaterials.resize(uniqueMaterialMap.size());
    for(const auto& [materialDef, index] : uniqueMaterialMap)
    {
        uniqueMaterials[index.Value()] = materialDef;
    }

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<Mesh> meshes;
    std::vector<Model> models;
    std::vector<char> stringStorage;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount);
    meshes.reserve(meshCount);
    models.reserve(propKitDef.ModelDefs.size());
    stringStorage.reserve(totalStringSize);
    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        const size_t nameOffset = stringStorage.size();

        stringStorage.insert(stringStorage.end(), modelDef.Name.begin(), modelDef.Name.end());
        stringStorage.push_back('\0');

        Model model //
            {
                .Name = std::string_view(&stringStorage[nameOffset], modelDef.Name.size()),
                .FirstMesh = MeshIndex(meshes.size()),
                .MeshCount = narrow_cast<uint32_t>(modelDef.MeshDefs.size()),
            };
        models.emplace_back(std::move(model));

        for(const auto& meshDef : modelDef.MeshDefs)
        {
            Mesh mesh //
                {
                    .IndexCount = narrow_cast<uint32_t>(meshDef.Indices.size()),
                    .FirstIndex = narrow_cast<uint32_t>(indices.size()),
                    .BaseVertex = narrow_cast<uint32_t>(vertices.size()),
                    .MaterialIndex = uniqueMaterialMap[meshDef.MaterialDef],
                    .BoundingBox = Box::FromVertices(meshDef.Vertices, meshDef.Indices),
                };

            vertices.insert(vertices.end(), meshDef.Vertices.begin(), meshDef.Vertices.end());
            indices.insert(indices.end(), meshDef.Indices.begin(), meshDef.Indices.end());
            meshes.emplace_back(std::move(mesh));
        }
    }

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(FetchTextures(rootPath, uniqueMaterials, textureCache, encoder));

    auto vertexBuffer = WebgpuHelper::CreateVertexBuffer(vertices, "VertexBuffer");
    MLG_CHECK(vertexBuffer);

    auto indexBuffer = WebgpuHelper::CreateIndexBuffer(indices, "IndexBuffer");
    MLG_CHECK(indexBuffer);

    auto materialConstantsBuffer = BuildMaterialConstantsBuffer(uniqueMaterials);
    MLG_CHECK(materialConstantsBuffer);

    std::vector<wgpu::BindGroup> materialBindGroups;
    MLG_CHECK(CreateMaterialBindGroups(uniqueMaterials, textureCache, materialBindGroups));

    wgpu::CommandBuffer commandBuffer = encoder.Finish();
    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    PropKit propKit(
        std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(meshes),
        std::move(models),
        std::move(*materialConstantsBuffer),
        std::move(materialBindGroups),
        std::move(stringStorage));

    outPropKit = std::move(propKit);

    MLG_INFO("PropKit created in {} ms", createTimer.ElapsedSeconds() * 1000);

    return Result<>::Ok;
}

PropKit::PropKit(VertexBuffer vertexBuffer,
    IndexBuffer indexBuffer,
    std::vector<Mesh>&& meshes,
    std::vector<Model>&& models,
    MaterialConstantsBuffer materialConstantsBuffer,
    std::vector<wgpu::BindGroup>&& materialBindGroups,
    std::vector<char>&& stringStorage)
    : m_VertexBuffer(vertexBuffer),
      m_IndexBuffer(indexBuffer),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_MaterialConstantsBuffer(materialConstantsBuffer),
      m_MaterialBindGroups(std::move(materialBindGroups)),
      m_StringStorage(std::move(stringStorage))
{
#ifndef NDEBUG
    for(const auto& mesh : m_Meshes)
    {
        const Vec3f& halfExtents = mesh.BoundingBox.GetHalfExtents();
        MLG_ASSERT(halfExtents != Vec3f{0}, "Mesh has degenerate bounding box");
        MLG_ASSERT(halfExtents.x >= 0 && halfExtents.y >= 0 && halfExtents.z >= 0,
            "Mesh has invalid bounding box");
    }
#endif // NDEBUG

    m_ModelNameToIndex.reserve(m_Models.size());
    for(uint32_t i = 0; i < static_cast<uint32_t>(m_Models.size()); ++i)
    {
        const Model& model = m_Models[i];
        MLG_ASSERT(!m_ModelNameToIndex.contains(model.Name), "Duplicate model name: {}", model.Name);
        m_ModelNameToIndex[model.Name] = ModelIndex(i);
    }
}