#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#define __LOGGER_NAME__ "PPKT"

#include "PropKit.h"
#include "FileFetcher.h"
#include "Log.h"
#include "Stopwatch.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <atomic>
#include <filesystem>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

namespace
{
struct TextureBuilder
{
    std::string Uri;
    const FileFetcher::Request* Request{ nullptr };
    Texture Texture;
    void* MappedMemory{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
};

template<typename T, typename U>
inline static T narrow_cast(U u) noexcept
{
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, "narrow_cast requires integral types");
    static_assert((std::numeric_limits<T>::is_signed == std::numeric_limits<U>::is_signed) ||
                      (std::numeric_limits<T>::is_signed && !std::numeric_limits<U>::is_signed),
        "narrow_cast requires both types to have the same signedness, or the destination type to be signed and the source type to be unsigned");
    static_assert(std::numeric_limits<T>::digits <= std::numeric_limits<U>::digits,
        "narrow_cast requires the destination type to have fewer or the same digits than the source type");
    const T t = static_cast<T>(u);
    MLG_ASSERT(static_cast<U>(t) == u, "narrow_cast failed: {} -> {}", u, t);
    return t;
}
}

static Result<>
StageTexture(TextureBuilder& builder)
{
    MLG_DEBUG("Staging texture...");

    int width, height, numChannels;

    if(!stbi_info_from_memory(builder.Request->Data.data(),
           narrow_cast<int>(builder.Request->Data.size()),
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

    auto mapped = texture->Map();
    MLG_CHECK(mapped);

    builder.Texture = *texture;
    builder.MappedMemory = *mapped;

    auto decode = [](void* userData)
    {
        auto builder = reinterpret_cast<TextureBuilder*>(userData);
        MLG_LOG_SCOPE(builder->Uri);

        MLG_DEBUG("Decoding...");

        int width, height, numChannels;
        stbi_uc* data = stbi_load_from_memory(builder->Request->Data.data(),
            narrow_cast<int>(builder->Request->Data.size()),
            &width,
            &height,
            &numChannels,
            kNumTextureChannels);

        if(!data)
        {
            MLG_ERROR("Error decoding - {}", stbi_failure_reason());
        }
        else
        {
            MLG_ASSERT(builder->Texture.GetWidth() == static_cast<uint32_t>(width) &&
                builder->Texture.GetHeight() == static_cast<uint32_t>(height),
                "Decoded image dimensions do not match texture dimensions - {}",
                builder->Uri);
            MLG_ASSERT(builder->Texture.GetFormat() == kTextureFormat,
                "Texture format does not match expected format - {}",
                builder->Uri);

            uint8_t* dst = (uint8_t*)builder->MappedMemory;
            const uint8_t* src = data;
            const uint32_t rowStride = width * kNumTextureChannels;
            const uint32_t alignedRowStride = (rowStride + 255) & ~255;
            for(uint32_t y = 0; y < static_cast<uint32_t>(height);
                ++y, dst += alignedRowStride, src += rowStride)
            {
                ::memcpy(dst, src, rowStride);
            }
            stbi_image_free(data);
        }

        builder->DecodeComplete = true;
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
            MLG_WARN("Failed to fetch texture: {}", request.FilePath);
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

static Result<wgpu::BindGroup>
CreateMaterialBindGroup(const MaterialDef& materialDef, const TextureCache& textureCache)
{
    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    Texture baseTexture =
        materialDef.BaseTextureUri.empty()
            ? textureCache.GetDefaultTexture()
            : textureCache.Get(materialDef.BaseTextureUri);

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
        .layout = (*bgLayouts)[2],
        .entryCount = std::size(bgEntries),
        .entries = bgEntries,
    };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bindGroupDesc);

    return bindGroup;
}

static Result<>
CreateMaterialBindGroups(std::span<const MaterialDef> materialDefs,
    const TextureCache& textureCache,
    std::vector<wgpu::BindGroup>& materialBindGroups)
{
    materialBindGroups.clear();

    materialBindGroups.reserve(materialDefs.size());

    for(const auto& mtl : materialDefs)
    {
        auto bindGroup = CreateMaterialBindGroup(mtl, textureCache);
        MLG_CHECK(bindGroup);

        materialBindGroups.emplace_back(std::move(*bindGroup));
    }

    return Result<>::Ok;
}

static Result<VertexBuffer>
BuildVertexBuffer(std::span<const Vertex> vertices, wgpu::CommandEncoder encoder)
{
    const size_t sizeofBuffer = vertices.size() * sizeof(Vertex);
    auto buffer = WebgpuHelper::CreateVertexBuffer(sizeofBuffer, "VertexBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    std::memcpy(*mapped, vertices.data(), sizeofBuffer);

    MLG_CHECK(buffer->Unmap(encoder));

    return *buffer;
}

static Result<IndexBuffer>
BuildIndexBuffer(std::span<const VertexIndex> indices, wgpu::CommandEncoder encoder)
{
    const size_t sizeofBuffer = indices.size() * sizeof(VertexIndex);
    auto buffer = WebgpuHelper::CreateIndexBuffer(sizeofBuffer, "IndexBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    std::memcpy(*mapped, indices.data(), sizeofBuffer);

    MLG_CHECK(buffer->Unmap(encoder));

    return *buffer;
}

static Result<MaterialConstantsBuffer>
BuildMaterialConstantsBuffer(std::span<const MaterialDef> materialDefs, wgpu::CommandEncoder encoder)
{
    const size_t sizeofBuffer = materialDefs.size() * sizeof(ShaderTypes::MaterialConstants);

    auto buffer = WebgpuHelper::CreateTypedStorageBuffer<MaterialConstantsBuffer>(sizeofBuffer,
        "MaterialConstantsBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderTypes::MaterialConstants* dst = *mapped;

    for(const auto& mtl : materialDefs)
    {
        ::new(dst++) ShaderTypes::MaterialConstants //
        {
            .Color = mtl.Color,
            .Metalness = mtl.Metalness,
            .Roughness = mtl.Roughness,
        };
    }

    buffer->Unmap(encoder);

    return buffer;
}

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

    size_t vertexCount = 0, indexCount = 0, meshCount = 0;
    uint32_t materialIndex = 0;

    std::map<MaterialDef, uint32_t, std::less<MaterialDef>> uniqueMaterialMap;
    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        for(const auto& mesh : modelDef.MeshDefs)
        {
            const MaterialDef& materialDef = mesh.MaterialDef;
            if(!uniqueMaterialMap.contains(materialDef))
            {
                uniqueMaterialMap[materialDef] = materialIndex++;
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
        uniqueMaterials[index] = materialDef;
    }

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<Mesh> meshes;
    std::vector<Model> models;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount);
    meshes.reserve(meshCount);
    models.reserve(propKitDef.ModelDefs.size());
    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        Model model //
            {
                .Name = modelDef.Name,
                .FirstMesh = narrow_cast<uint32_t>(meshes.size()),
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
                    .BoundingBox = AABoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices),
                };

            vertices.insert(vertices.end(), meshDef.Vertices.begin(), meshDef.Vertices.end());
            indices.insert(indices.end(), meshDef.Indices.begin(), meshDef.Indices.end());
            meshes.emplace_back(std::move(mesh));

        }
    }

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(FetchTextures(rootPath, uniqueMaterials, textureCache, encoder));

    auto vertexBuffer = BuildVertexBuffer(vertices, encoder);
    MLG_CHECK(vertexBuffer);

    auto indexBuffer = BuildIndexBuffer(indices, encoder);
    MLG_CHECK(indexBuffer);

    auto materialConstantsBuffer = BuildMaterialConstantsBuffer(uniqueMaterials, encoder);
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
        std::move(materialBindGroups));

    outPropKit = std::move(propKit);

    MLG_INFO("PropKit created in {} ms", createTimer.ElapsedSeconds() * 1000);

    return Result<>::Ok;
}

PropKit::PropKit(VertexBuffer vertexBuffer,
    IndexBuffer indexBuffer,
    std::vector<Mesh>&& meshes,
    std::vector<Model>&& models,
    MaterialConstantsBuffer materialConstantsBuffer,
    std::vector<wgpu::BindGroup>&& materialBindGroups)
    : m_IndexBuffer(indexBuffer),
      m_VertexBuffer(vertexBuffer),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_MaterialConstantsBuffer(materialConstantsBuffer),
      m_MaterialBindGroups(std::move(materialBindGroups))
{
#ifndef NDEBUG
    for(const auto& mesh : m_Meshes)
    {
        const Vec3f& aabbMax = mesh.BoundingBox.GetMax();
        const Vec3f& aabbMin = mesh.BoundingBox.GetMin();
        MLG_ASSERT(aabbMin != aabbMax, "Mesh has degenerate bounding box");
        MLG_ASSERT(aabbMin.x <= aabbMax.x && aabbMin.y <= aabbMax.y && aabbMin.z <= aabbMax.z,
            "Mesh has invalid bounding box");
    }
#endif // NDEBUG

    m_ModelNameToIndex.reserve(m_Models.size());
    for(uint32_t i = 0; i < static_cast<uint32_t>(m_Models.size()); ++i)
    {
        const Model& model = m_Models[i];
        MLG_ASSERT(!m_ModelNameToIndex.contains(model.Name), "Duplicate model name: {}", model.Name);
        m_ModelNameToIndex[model.Name] = i;
    }
}