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
    TextureBuilder(std::string baseUri, FileFetcher::Request* request, std::atomic<unsigned>* stageCounter)
        : Uri(std::move(baseUri)),
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
    FileFetcher::Request* Request{ nullptr };
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
    std::vector<FileFetcher::Request> requestHeap;
    std::vector<TextureBuilder> fetching;
    std::vector<TextureBuilder> staging;

    // Allocate a contiguous block (a heap) of fetch requests.
    // Pointers to pending fetch requests will be used to initialize
    // instances of TextureBuilder.
    requestHeap.reserve(materialDefs.size());

    // Collection of builders for which we're fetching texture files.
    fetching.reserve(materialDefs.size());

    // Collection of builders for which fetching is complete and we're now staging the textures to
    // the GPU.
    staging.reserve(materialDefs.size());

    // Counter to track how many staging operations have completed.
    // We wait on this to signal completion of all stating operations.
    std::atomic<unsigned> stageCounter{0};

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

        FileFetcher::Request& request =
            requestHeap.emplace_back((basePath / mtl.BaseTextureUri).string());
        fetching.emplace_back(mtl.BaseTextureUri, &request, &stageCounter);

        MLG_DEBUG("Fetching texture...");

        if(!FileFetcher::Fetch(request))
        {
            MLG_WARN("Failed to fetch texture");
        }
    }

    while(!fetching.empty())
    {
        FileFetcher::ProcessCompletions();

        for(size_t i = 0; i < fetching.size();)
        {
            TextureBuilder& fetchingBuilder = fetching[i];

            if(fetchingBuilder.Request->IsPending())
            {
                ++i;
                continue;
            }

            // Move the builder to the staging list.
            TextureBuilder& stagingBuilder = staging.emplace_back(std::move(fetchingBuilder));

            // Fill the hole.
            fetching[i] = std::move(fetching.back());
            fetching.pop_back();

            if(stagingBuilder.Request->Succeeded())
            {
                // Add one to the stage counter.  Staging ops will decrement the counter
                // and when it hits zero we know we're done.
                stageCounter.fetch_add(1, std::memory_order_relaxed);

                auto result = StageTexture(stagingBuilder);
                if(!result)
                {
                    MLG_WARN("Failed to stage texture: {}", stagingBuilder.Uri);
                    stageCounter.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            else
            {
                MLG_WARN("Failed to fetch texture: {}", stagingBuilder.Uri);
            }
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
    for(auto& builder : staging)
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
        const Texture& baseTexture = mtlDef.BaseTextureUri.empty()
                                         ? textureCache.GetDefaultTexture()
                                         : textureCache.Get(mtlDef.BaseTextureUri);

        const ColorShaderContract::MaterialGroup::Resources resources //
            {
                .BaseTexture = baseTexture,
                .BaseSampler = textureCache.GetDefaultSampler(),
            };

        wgpu::BindGroup bindGroup =
            ColorShaderContract::MaterialGroup::CreateBindGroup(WebgpuHelper::GetDevice(),
                *bgLayout,
                resources);

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
    size_t materialId = 0;

    std::map<MaterialDef, MaterialIdentifier> uniqueMaterialMap;

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
                uniqueMaterialMap[materialDef] = MaterialIdentifier(materialId++);
            }

            vertexCount += mesh.Vertices.size();
            indexCount += mesh.Indices.size();
            meshCount += 1;
        }
    }

    std::vector<MaterialDef> uniqueMaterials;
    uniqueMaterials.resize(uniqueMaterialMap.size());
    for(const auto& [materialDef, id] : uniqueMaterialMap)
    {
        uniqueMaterials[id.GetValue()] = materialDef;
    }

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<Mesh> meshes;
    std::vector<Model> models;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount);
    meshes.reserve(meshCount);
    models.reserve(propKitDef.ModelDefs.size());
    StringArena stringArena(totalStringSize);

    for(const auto& modelDef : propKitDef.ModelDefs)
    {
        const StringHandle modelName = stringArena.NewString(modelDef.Name);

        const size_t firstMeshIdx = meshes.size();

        for(const auto& meshDef : modelDef.MeshDefs)
        {
            const Mesh mesh(
                {
                    .IndexCount = narrow_cast<uint32_t>(meshDef.Indices.size()),
                    .FirstIndex = narrow_cast<uint32_t>(indices.size()),
                    .BaseVertex = narrow_cast<uint32_t>(vertices.size()),
                },
                uniqueMaterialMap[meshDef.MaterialDef],
                BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices));

            vertices.insert(vertices.end(), meshDef.Vertices.begin(), meshDef.Vertices.end());
            indices.insert(indices.end(), meshDef.Indices.begin(), meshDef.Indices.end());
            meshes.emplace_back(mesh);
        }

        const std::span<const Mesh> meshSpan = std::span<const Mesh>(meshes).subspan(firstMeshIdx);
        BoundingBox boundingBox = meshSpan.front().GetBoundingBox();
        for(const Mesh& mesh : meshSpan.subspan(1))
        {
            boundingBox += mesh.GetBoundingBox();
        }

        const Model model(modelName,
            MeshIdentifier(firstMeshIdx),
            modelDef.MeshDefs.size(),
            boundingBox,
            BoundingSphere(boundingBox));
        models.emplace_back(model);
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
        std::move(stringArena));

    MLG_INFO("PropKit created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(propKit);
}

Result<std::span<const Mesh>>
PropKit::GetMeshes(const ModelIdentifier& modelId) const
{
    MLG_CHECKV(modelId.IsValid() && modelId.GetValue() < m_Models.size(),
        "Invalid model id: {}",
        modelId.GetValue());

    const Model& model = m_Models[modelId.GetValue()];

    MLG_CHECKV(model.GetFirstMeshId().IsValid() &&
                   model.GetFirstMeshId().GetValue() + model.GetMeshCount() <= m_Meshes.size(),
        "Model has invalid mesh range: {}, {}",
        model.GetFirstMeshId().GetValue(),
        model.GetMeshCount());

    return std::span<const Mesh>(&m_Meshes[model.GetFirstMeshId().GetValue()], model.GetMeshCount());
}

Result<BoundingSphere>
PropKit::GetBoundingSphere(const ModelIdentifier& modelId) const
{
    MLG_CHECKV(modelId.IsValid() && modelId.GetValue() < m_Models.size(),
        "Invalid model id: {}",
        modelId.GetValue());

    const Model& model = m_Models[modelId.GetValue()];

    return model.GetBoundingSphere();
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

PropKit::PropKit(VertexBuffer vertexBuffer,
    IndexBuffer indexBuffer,
    std::vector<Mesh> meshes,
    std::vector<Model> models,
    MaterialConstantsBuffer materialConstantsBuffer,
    std::vector<wgpu::BindGroup> materialBindGroups,
    StringArena stringArena)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_MaterialConstantsBuffer(std::move(materialConstantsBuffer)),
      m_MaterialBindGroups(std::move(materialBindGroups)),
      m_StringArena(std::move(stringArena))
{
#ifndef NDEBUG
    for(const auto& mesh : m_Meshes)
    {
        const Vec3f& halfExtents = mesh.GetBoundingBox().GetHalfExtents();
        MLG_ASSERT(halfExtents != Vec3f{0}, "Mesh has degenerate bounding box");
        MLG_ASSERT(halfExtents.x >= 0 && halfExtents.y >= 0 && halfExtents.z >= 0,
            "Mesh has invalid bounding box");
    }
#endif // NDEBUG

    m_ModelNameToId.reserve(m_Models.size());
    for(size_t i = 0; i < m_Models.size(); ++i)
    {
        const Model& model = m_Models[i];
        MLG_ASSERT(!m_ModelNameToId.contains(model.GetName()), "Duplicate model name: {}", model.GetName());
        m_ModelNameToId[model.GetName()] = ModelIdentifier(i);
    }
}