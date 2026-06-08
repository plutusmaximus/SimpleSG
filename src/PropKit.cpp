#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PPKT"

#include "PropKit.h"
#include "FileFetcher.h"
#include "Log.h"
#include "narrow_cast.h"
#include "scope_exit.h"
#include "Timer.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <atomic>
#include <filesystem>
#include <map>

#include <stb_image.h>

static constexpr size_t kNumTextureChannels = 4;

namespace
{
class TextureBuilder
{
public:
    TextureBuilder(std::string uri,
        const FileFetcher::Request* request,
        std::atomic<unsigned>* stageCounter)
        : Uri(std::move(uri)),
          Request(request),
          StageCounter(stageCounter)
    {
    }

    TextureBuilder() = delete;
    ~TextureBuilder() = default;
    TextureBuilder(const TextureBuilder&) = delete;
    TextureBuilder& operator=(const TextureBuilder&) = delete;
    TextureBuilder(TextureBuilder&&) = default;
    TextureBuilder& operator=(TextureBuilder&&) = default;

    void Decode()
    {
        DecodeResult = Decode(*this);
    }

    static Result<> Decode(const TextureBuilder& builder)
    {
        MLG_DEFER
        {
            const unsigned oldValue = builder.StageCounter->fetch_sub(1, std::memory_order_acq_rel);
            if(oldValue == 1)
            {
                // Last staging finished, signal completion.
                builder.StageCounter->notify_all();
            }
        };

        MLG_LOG_SCOPE(builder.Uri);

        MLG_DEBUG("Decoding...");

        int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;
        stbi_uc* data = stbi_load_from_memory(builder.Request->GetData().data(),
            narrow_cast<int>(builder.Request->GetData().size()),
            &imgWidth,
            &imgHeight,
            &imgNumChannels,
            kNumTextureChannels);

        MLG_CHECKV(data, "Failed to decode image {} - {}", builder.Uri, stbi_failure_reason());

        MLG_DEFER{ stbi_image_free(data); };

        MLG_CHECKV(builder.Texture->GetWidth() == static_cast<uint32_t>(imgWidth) &&
                       builder.Texture->GetHeight() == static_cast<uint32_t>(imgHeight),
            "Decoded image dimensions do not match texture dimensions - {}",
            builder.Uri);

        MLG_CHECKV(builder.Texture->GetFormat() == wgpu::TextureFormat::RGBA8Unorm,
            "Texture format does not match expected format - {}",
            builder.Uri);

        const size_t sizeofData = static_cast<size_t>(imgWidth) * static_cast<size_t>(imgHeight) *
                                  static_cast<size_t>(kNumTextureChannels);

        const size_t expectedSize = static_cast<size_t>(builder.Texture->GetWidth()) *
                                                    static_cast<size_t>(builder.Texture->GetHeight()) *
                                                    static_cast<size_t>(kNumTextureChannels);

        MLG_CHECKV(sizeofData == expectedSize,
            "Decoded image size does not match texture size - {}",
            builder.Uri);

        const std::span<const stbi_uc> srcSpan(data, sizeofData);
        const std::span<std::byte> dstSpan(builder.MappedMemory, sizeofData);
        size_t dstOffset = 0, srcOffset = 0;
        const size_t srcRowStride = static_cast<size_t>(imgWidth) * kNumTextureChannels;
        const size_t dstRowStride = (srcRowStride + 255) & ~255uz;
        for(int y = 0; y < imgHeight; ++y, dstOffset += dstRowStride, srcOffset += srcRowStride)
        {
            ::memcpy(&dstSpan[dstOffset], &srcSpan[srcOffset], srcRowStride);
        }

        return Result<>::Ok;
    }

    std::string Uri;
    const FileFetcher::Request* Request{ nullptr };
    Result<Texture> Texture;
    std::byte* MappedMemory{ nullptr };
    Result<> DecodeResult;

    // Counter to track how many staging operations have completed.
    std::atomic<unsigned>* StageCounter{ nullptr };
};

Result<>
StageTexture(TextureBuilder& builder)
{
    MLG_DEBUG("Staging texture...");

    int width = 0, height = 0, numChannels = 0;

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
    // as other wgpu::Device operations.  Learned that the hard way by trying to map
    // in the worker thread below.
    auto mapped = texture->MapBytes();
    MLG_CHECK(mapped);

    builder.Texture = std::move(*texture);
    builder.MappedMemory = mapped->data();

    auto decode = [](void* userData)
    {
        TextureBuilder* texBuilder = static_cast<TextureBuilder*>(userData);
        texBuilder->Decode();
    };

    ThreadPool::Enqueue(decode, &builder);

    return Result<>::Ok;
}

Result<>
FetchTextures(const std::filesystem::path& basePath,
    const std::span<const MaterialDef> materialDefs,
    TextureCache& textureCache,
    const wgpu::CommandEncoder& encoder)
{
    class RequestRecord
    {
    public:
        RequestRecord(std::string baseUri, std::string fullUri)
            : BaseUri(std::move(baseUri)),
              Request(std::move(fullUri))
        {
        }

        ~RequestRecord() = default;
        RequestRecord(const RequestRecord&) = delete;
        RequestRecord& operator=(const RequestRecord&) = delete;
        RequestRecord(RequestRecord&& other) = delete;
        RequestRecord& operator=(RequestRecord&& other) = delete;

        std::string BaseUri;
        FileFetcher::Request Request;
    };

    std::vector<std::unique_ptr<RequestRecord>> pendingRequests;
    std::vector<std::unique_ptr<RequestRecord>> completedRequests;
    std::vector<TextureBuilder> textureBuilders;

    pendingRequests.reserve(materialDefs.size());
    completedRequests.reserve(materialDefs.size());
    textureBuilders.reserve(materialDefs.size());

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

        auto requestRecPtr = std::make_unique<RequestRecord>(mtl.BaseTextureUri,
            (basePath / mtl.BaseTextureUri).string());
        RequestRecord& requestRec = *requestRecPtr;
        pendingRequests.emplace_back(std::move(requestRecPtr));

        MLG_DEBUG("Fetching texture...");

        if(!FileFetcher::Fetch(requestRec.Request))
        {
            MLG_WARN("Failed to fetch texture: {}", requestRec.BaseUri);
        }
    }

    // Counter to track how many staging operations have completed.
    std::atomic<unsigned> stageCounter{0};

    while(!pendingRequests.empty())
    {
        FileFetcher::ProcessCompletions();

        for(size_t i = 0; i < pendingRequests.size();)
        {
            auto& pendingRqst = pendingRequests[i];

            if(pendingRqst->Request.IsPending())
            {
                ++i;
                continue;
            }

            if(pendingRqst->Request.Succeeded())
            {
                stageCounter.fetch_add(1, std::memory_order_relaxed);
                TextureBuilder& builder = textureBuilders.emplace_back(pendingRqst->BaseUri,
                    &pendingRqst->Request,
                    &stageCounter);

                auto result = StageTexture(builder);
                if(!result)
                {
                    MLG_WARN("Failed to stage texture: {}", builder.Uri);
                    stageCounter.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            else
            {
                MLG_WARN("Failed to fetch texture: {}", pendingRqst->BaseUri);
            }

            completedRequests.push_back(std::move(pendingRqst));
            pendingRequests[i] = std::move(pendingRequests.back());
            pendingRequests.pop_back();
        }
    }

    // Wait until the stage counter reaches zero.
    for(unsigned value = stageCounter.load(std::memory_order_acquire); value != 0;
        value = stageCounter.load(std::memory_order_acquire))
    {
        stageCounter.wait(value, std::memory_order_acquire);
    }

    // Unmap textures to flush the data to the GPU before adding them to the cache.
    // It appears that mapping/unmapping must be done on the same thread
    // as other wgpu::Device operations.
    for(auto& builder : textureBuilders)
    {
        MLG_LOG_SCOPE(builder.Uri);

        if(!builder.DecodeResult)
        {
            MLG_ERROR("Failed to decode texture: {}", builder.Uri);
            continue;
        }

        builder.Texture->Unmap(encoder);
        textureCache.AddOrReplace(builder.Uri, std::move(*builder.Texture));
    }

    return Result<>::Ok;
}

Result<>
CreateMaterialBindGroups(const std::span<const MaterialDef> materialDefs,
    const TextureCache& textureCache,
    std::vector<wgpu::BindGroup>& materialBindGroups)
{
    materialBindGroups.clear();

    materialBindGroups.reserve(materialDefs.size());

    auto bgLayout = WebgpuHelper::GetTextureSamplerBindGroupLayout();
    MLG_CHECK(bgLayout);

    for(const auto& mtlDef : materialDefs)
    {
        const Texture& baseTexture =
            mtlDef.BaseTextureUri.empty()
                ? textureCache.GetDefaultTexture()
                : textureCache.Get(mtlDef.BaseTextureUri);

        const wgpu::BindGroupEntry bgEntries[]//
        {
            {
                .binding = 0,
                .textureView = baseTexture.GetGpuTexture().CreateView(),
            },
            {
                .binding = 1,
                .sampler = textureCache.GetDefaultSampler(),
            },
        };

        const wgpu::BindGroupDescriptor bindGroupDesc //
        {
            .label = "MaterialBindGroup",
            .layout = *bgLayout,
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

        wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bindGroupDesc);

        materialBindGroups.emplace_back(std::move(bindGroup));
    }

    return Result<>::Ok;
}

Result<MaterialConstantsBuffer>
BuildMaterialConstantsBuffer(const std::span<const MaterialDef> materialDefs)
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

        materialConstants.emplace_back(mc);
    }

    return WebgpuHelper::CreateStorageBuffer<MaterialConstantsBuffer>(materialConstants,
        "MaterialConstantsBuffer");
}
} // namespace

Result<PropKit>
PropKit::Create(
    const std::filesystem::path& rootPath, TextureCache& textureCache, const PropKitDef& propKitDef)
{
    Timer createTimer;
    createTimer.Start();

    size_t vertexCount = 0, indexCount = 0, meshCount = 0, totalStringSize = 0;
    uint32_t materialIndex = 0;

    std::map<MaterialDef, MaterialIndex> uniqueMaterialMap;

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

        const Model model //
            {
                .Name = std::string_view(&stringStorage[nameOffset], modelDef.Name.size()),
                .FirstMesh = MeshIndex(meshes.size()),
                .MeshCount = narrow_cast<uint32_t>(modelDef.MeshDefs.size()),
            };
        models.emplace_back(model);

        for(const auto& meshDef : modelDef.MeshDefs)
        {
            const Mesh mesh //
                {
                    .IndexCount = narrow_cast<uint32_t>(meshDef.Indices.size()),
                    .FirstIndex = narrow_cast<uint32_t>(indices.size()),
                    .BaseVertex = narrow_cast<uint32_t>(vertices.size()),
                    .MaterialIndex = uniqueMaterialMap[meshDef.MaterialDef],
                    .BoundingBox = Box::FromVertices(meshDef.Vertices, meshDef.Indices),
                };

            vertices.insert(vertices.end(), meshDef.Vertices.begin(), meshDef.Vertices.end());
            indices.insert(indices.end(), meshDef.Indices.begin(), meshDef.Indices.end());
            meshes.emplace_back(mesh);
        }
    }

    const wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(FetchTextures(rootPath, uniqueMaterials, textureCache, encoder));

    auto vertexBuffer = WebgpuHelper::CreateVertexBuffer(vertices, "VertexBuffer");
    MLG_CHECK(vertexBuffer);

    auto indexBuffer = WebgpuHelper::CreateIndexBuffer(indices, "IndexBuffer");
    MLG_CHECK(indexBuffer);

    auto materialConstantsBuffer = BuildMaterialConstantsBuffer(uniqueMaterials);
    MLG_CHECK(materialConstantsBuffer);

    std::vector<wgpu::BindGroup> materialBindGroups;
    MLG_CHECK(CreateMaterialBindGroups(uniqueMaterials, textureCache, materialBindGroups));

    const wgpu::CommandBuffer commandBuffer = encoder.Finish();
    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    PropKit propKit(
        std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(meshes),
        std::move(models),
        std::move(*materialConstantsBuffer),
        std::move(materialBindGroups),
        std::move(stringStorage));

    MLG_INFO("PropKit created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(propKit);
}

Result<std::span<const Mesh>>
PropKit::GetMeshes(const ModelIndex& modelIndex) const
{
    MLG_CHECKV(modelIndex.Value() < m_Models.size(), "Invalid model index: {}", modelIndex.Value());
    const Model& model = m_Models[modelIndex.Value()];
    return std::span<const Mesh>(&m_Meshes[model.FirstMesh.Value()], model.MeshCount);
}

// private:

PropKit::PropKit(VertexBuffer vertexBuffer,
    IndexBuffer indexBuffer,
    std::vector<Mesh> meshes,
    std::vector<Model> models,
    MaterialConstantsBuffer materialConstantsBuffer,
    std::vector<wgpu::BindGroup> materialBindGroups,
    std::vector<char> stringStorage)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_MaterialConstantsBuffer(std::move(materialConstantsBuffer)),
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