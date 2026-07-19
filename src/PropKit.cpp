#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "PROP"

#include "PropKit.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "LevelDefs.h"
#include "Log.h"
#include "narrow_cast.h"
#include "scope_exit.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "Timer.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <ranges>
#include <stb_image.h>

static constexpr size_t kNumTextureChannels = 4;

namespace
{
class TextureLoadTask
{
public:
    TextureLoadTask(const std::filesystem::path& basePath,
        const std::string_view& baseUri,
        std::atomic<bool>* isComplete)
        : Uri(baseUri),
          Request(basePath / baseUri),
          IsComplete(isComplete)
    {
        IsComplete->store(false, std::memory_order_release);
    }

    TextureLoadTask() = delete;
    ~TextureLoadTask() = default;
    TextureLoadTask(const TextureLoadTask&) = delete;
    TextureLoadTask& operator=(const TextureLoadTask&) = delete;
    TextureLoadTask(TextureLoadTask&&) = default;
    TextureLoadTask& operator=(TextureLoadTask&&) = default;

    void Decode() { DecodeResult = Decode(*this); }

    static Result<> Decode(const TextureLoadTask& tlTask)
    {
        MLG_DEFER
        {
            tlTask.IsComplete->store(true, std::memory_order_release);
        };

        MLG_LOG_SCOPE(tlTask.Uri);

        MLG_DEBUG("Decoding...");

        int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;
        stbi_uc* data = stbi_load_from_memory(tlTask.Request.GetData().data(),
            narrow_cast<int>(tlTask.Request.GetData().size()),
            &imgWidth,
            &imgHeight,
            &imgNumChannels,
            kNumTextureChannels);

        MLG_CHECKV(data, "Failed to decode image - {}", stbi_failure_reason());

        MLG_DEFER
        {
            stbi_image_free(data);
        };

        MLG_CHECKV(tlTask.Texture.GetWidth() == static_cast<uint32_t>(imgWidth)
                && tlTask.Texture.GetHeight() == static_cast<uint32_t>(imgHeight),
            "Decoded image dimensions do not match texture dimensions");

        MLG_CHECKV(tlTask.Texture.GetFormat() == wgpu::TextureFormat::RGBA8Unorm,
            "Texture format does not match expected format");

        const size_t sizeofData =
            static_cast<size_t>(imgWidth) * static_cast<size_t>(imgHeight) * kNumTextureChannels;

        const size_t expectedSize = static_cast<size_t>(tlTask.Texture.GetWidth())
            * static_cast<size_t>(tlTask.Texture.GetHeight())
            * kNumTextureChannels;

        MLG_CHECKV(sizeofData == expectedSize, "Decoded image size does not match texture size");

        const std::span<const stbi_uc> srcSpan(data, sizeofData);
        const std::span<std::byte> dstSpan(tlTask.MappedMemory, sizeofData);
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
    FileFetcher::Request Request;
    wgpu::Texture Texture;
    wgpu::Buffer StagingBuffer;
    std::byte* MappedMemory{ nullptr };
    Result<> DecodeResult;

    // Counter to track how many staging operations have completed.
    std::atomic<bool>* IsComplete{ nullptr };
};

Result<>
StageTexture(GpuHelper& gpuHelper, ThreadPool& threadPool, TextureLoadTask& tlTask)
{
    MLG_DEBUG("Staging texture...");

    int width = 0, height = 0, numChannels = 0;

    if(!stbi_info_from_memory(tlTask.Request.GetData().data(),
           narrow_cast<int>(tlTask.Request.GetData().size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", tlTask.Uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto texture = gpuHelper.CreateTexture(static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        tlTask.Uri);

    MLG_CHECK(texture);

    auto stagingBuffer = gpuHelper.CreateStagingBuffer(*texture, tlTask.Uri);
    MLG_CHECK(stagingBuffer);

    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    // It appears that mapping/unmapping must be done on the same thread
    // as other wgpu::Device operations.  Learned that the hard way by trying to map
    // in the worker thread below.
    tlTask.Texture = *texture;
    tlTask.StagingBuffer = *stagingBuffer;
    tlTask.MappedMemory = static_cast<std::byte*>(mapped);

    auto decode = [](void* userData) { static_cast<TextureLoadTask*>(userData)->Decode(); };

    threadPool.Enqueue(decode, &tlTask);

    return Result<>::Ok;
}

Result<>
FetchTextures(GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& basePath,
    const std::span<const MaterialDef> materialDefs,
    TextureCache& textureCache)
{
    // Heap of texture load tasks.
    std::vector<TextureLoadTask> tlTaskHeap;
    // Collection of tasks for which we're fetching texture files.
    std::vector<TextureLoadTask*> fetching;
    // Collection of tasks for which fetching is complete and we're now staging the textures
    // to the GPU.
    std::vector<TextureLoadTask*> staging;

    std::vector<std::atomic<bool>> completionFlags(materialDefs.size());

    tlTaskHeap.reserve(materialDefs.size());

    fetching.reserve(materialDefs.size());

    staging.reserve(materialDefs.size());

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

        const size_t index = tlTaskHeap.size();

        TextureLoadTask& task =
            tlTaskHeap.emplace_back(basePath, mtl.BaseTextureUri, &completionFlags[index]);

        MLG_DEBUG("Fetching texture...");

        if(fileFetcher.Fetch(task.Request))
        {
            fetching.emplace_back(&task);
        }
        else
        {
            MLG_WARN("Failed to fetch texture");
        }
    }

    while(!fetching.empty())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());

        for(size_t i = 0; i < fetching.size();)
        {
            TextureLoadTask* tlTask = fetching[i];

            if(tlTask->Request.IsPending())
            {
                ++i;
                continue;
            }

            // Remove from the fetching list.
            fetching[i] = std::move(fetching.back());
            fetching.pop_back();

            MLG_LOG_SCOPE(tlTask->Uri);

            if(tlTask->Request.Succeeded())
            {
                if(StageTexture(gpuHelper, threadPool, *tlTask))
                {
                    staging.emplace_back(tlTask);
                }
                else
                {
                    MLG_WARN("Failed to stage texture");
                }
            }
            else
            {
                MLG_WARN("Failed to fetch texture");
            }
        }
    }

    const wgpu::CommandEncoder encoder = gpuHelper.GetDevice().CreateCommandEncoder();

    while(!staging.empty())
    {
        for(size_t i = 0; i < staging.size();)
        {
            const TextureLoadTask* tlTask = staging[i];

            if(!tlTask->IsComplete->load(std::memory_order_acquire))
            {
                ++i;
                continue;
            }

            MLG_LOG_SCOPE(tlTask->Uri);

            if(!tlTask->DecodeResult)
            {
                MLG_ERROR("Failed to decode texture");
            }
            else if(!GpuHelper::CommitStagingBuffer(tlTask->Texture, tlTask->StagingBuffer, encoder))
            {
                MLG_ERROR("Failed to commit texture");
            }
            else
            {
                MLG_DEBUG("Loaded");
                textureCache.AddOrReplace(tlTask->Uri, tlTask->Texture);
            }

            // Remove from the staging list.
            staging[i] = std::move(staging.back());
            staging.pop_back();
        }
    }

    const wgpu::CommandBuffer commandBuffer = encoder.Finish();
    gpuHelper.GetDevice().GetQueue().Submit(1, &commandBuffer);

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

        materialBindGroups.emplace_back(std::move(*bindGroup));
    }

    return Result<>::Ok;
}

Result<MaterialConstantsBuffer>
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

        materialConstants.emplace_back(mc);
    }

    auto buffer = gpuHelper.CreateStorageBuffer<MaterialConstantsBuffer>(materialConstants.size(),
        "MaterialConstants");

    MLG_CHECK(buffer);

    buffer->Store(materialConstants);

    return buffer;
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

    size_t vertexCount = 0, indexCount = 0, meshCount = 0, totalStringSize = 0;
    size_t materialIndex = 0;

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
                uniqueMaterialMap[materialDef] = MaterialIdentifier(materialIndex++);
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
            const MaterialIdentifier materialId = uniqueMaterialMap[meshDef.MaterialDef];
            const BoundingBox aabb = BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices);

            const Mesh mesh(
                {
                    .IndexCount = narrow_cast<uint32_t>(meshDef.Indices.size()),
                    .FirstIndex = narrow_cast<uint32_t>(indices.size()),
                    .BaseVertex = narrow_cast<uint32_t>(vertices.size()),
                },
                materialId,
                aabb);

            vertices.insert(vertices.end(), meshDef.Vertices.begin(), meshDef.Vertices.end());
            indices.insert(indices.end(), meshDef.Indices.begin(), meshDef.Indices.end());
            meshes.emplace_back(mesh);
        }

        const std::span<const Mesh> meshSpan = std::span<const Mesh>(meshes).subspan(firstMeshIdx);
        BoundingBox aabb = meshSpan.front().GetBoundingBox();
        for(const Mesh& mesh : meshSpan.subspan(1))
        {
            aabb += mesh.GetBoundingBox();
        }

        const Model model(modelName, meshSpan, aabb);
        models.emplace_back(model);
    }

    TextureCache textureCache(gpuHelper.GetDefaultTexture());

    MLG_CHECK(
        FetchTextures(gpuHelper, threadPool, fileFetcher, rootPath, uniqueMaterials, textureCache));

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
        std::move(meshes),
        std::move(models),
        std::move(stringArena));

    MLG_INFO("PropKit created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(propKit);
}

const Model*
PropKit::GetModel(const std::string_view& name) const
{
    auto it = std::ranges::lower_bound(m_Models, name, {}, &Model::GetName);

    if(!MLG_VERIFY(m_Models.end() != it && it->GetName() == name, "Model not found: {}", name))
    {
        return nullptr;
    }

    return &(*it);
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

PropKit::PropKit(VertexBuffer&& vertexBuffer,
    IndexBuffer&& indexBuffer,
    MaterialConstantsBuffer&& materialConstants,
    std::vector<wgpu::BindGroup>&& materialBindGroups,
    std::vector<Mesh>&& meshes,
    std::vector<Model>&& models,
    StringArena&& stringArena)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_MaterialConstants(std::move(materialConstants)),
      m_MaterialBindGroups(std::move(materialBindGroups)),
      m_Meshes(std::move(meshes)),
      m_Models(std::move(models)),
      m_StringArena(std::move(stringArena))
{
    std::ranges::sort(m_Models,
        [](const Model& a, const Model& b) { return a.GetName() < b.GetName(); });

    for(auto it = m_Models.begin() + 1; it != m_Models.end(); ++it)
    {
        const Model& a = *(it - 1);
        const Model& b = *it;

        if(!MLG_VERIFY(a.GetName() != b.GetName()))
        {
            MLG_ERROR("Duplicate model name found: {}", a.GetName());
        }
    }
}