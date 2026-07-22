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

namespace
{
class TextureLoadTask
{
public:
    enum class State
    {
        None,
        Fetch,
        Fetching,
        Decoding,
        Succeeded,
        Failed,
        Completed
    };

    TextureLoadTask(const std::filesystem::path& basePath,
        const std::string_view& baseUri,
        GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        TextureCache& textureCache,
        wgpu::CommandEncoder encoder,
        std::atomic<bool>* completionFlag)
        : m_Uri(baseUri),
          m_GpuHelper(&gpuHelper),
          m_FileFetcher(&fileFetcher),
          m_ThreadPool(&threadPool),
          m_TextureCache(&textureCache),
          m_Encoder(std::move(encoder)),
          m_Request(basePath / baseUri),
          m_CompletionFlag(completionFlag),
          m_State(State::Fetch)
    {
        m_CompletionFlag->store(false, std::memory_order_release);
    }

    TextureLoadTask() = delete;
    ~TextureLoadTask() = default;
    TextureLoadTask(const TextureLoadTask&) = delete;
    TextureLoadTask& operator=(const TextureLoadTask&) = delete;
    TextureLoadTask(TextureLoadTask&&) = default;
    TextureLoadTask& operator=(TextureLoadTask&&) = default;

    void Update();

    bool IsComplete() const { return m_CompletionFlag->load(std::memory_order_acquire); }

    Result<> Stage();

    Result<> Decode() const;

    static void Decode(void* userData)
    {
        TextureLoadTask* task = static_cast<TextureLoadTask*>(userData);
        task->m_DecodeResult = task->Decode();
        task->m_CompletionFlag->store(true, std::memory_order_release);
    }

private:
    std::string m_Uri;
    GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    TextureCache* m_TextureCache{ nullptr };
    wgpu::CommandEncoder m_Encoder{ nullptr };
    FileFetcher::Request m_Request;
    wgpu::Texture m_Texture;
    wgpu::Buffer m_StagingBuffer;
    std::byte* m_MappedMemory{ nullptr };
    Result<> m_DecodeResult;

    std::atomic<bool>* m_CompletionFlag{ nullptr };

    State m_State{ State::None };
};

void
TextureLoadTask::Update()
{
    MLG_LOG_SCOPE(m_Uri);

    switch(m_State)
    {
        case State::None:
            break;
        case State::Fetch:
            if(m_FileFetcher->Fetch(m_Request))
            {
                m_State = State::Fetching;
            }
            else
            {
                MLG_ERROR("Failed to fetch texture");
                m_State = State::Failed;
            }
            break;
        case State::Fetching:
            if(m_Request.Succeeded())
            {
                if(Stage())
                {
                    m_State = State::Decoding;
                }
                else
                {
                    MLG_ERROR("Failed to stage texture");
                    m_State = State::Failed;
                }
            }
            else if(!m_Request.IsPending())
            {
                MLG_ERROR("Failed to fetch texture");
                m_State = State::Failed;
            }
            break;
        case State::Decoding:
            if(m_CompletionFlag->load(std::memory_order_acquire))
            {
                if(!m_DecodeResult)
                {
                    MLG_ERROR("Failed to decode texture");
                    m_State = State::Failed;
                }
                else if(!GpuHelper::CommitStagingBuffer(m_Texture, m_StagingBuffer, m_Encoder))
                {
                    MLG_ERROR("Failed to commit texture");
                    m_State = State::Failed;
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    m_TextureCache->AddOrReplace(m_Uri, m_Texture);
                    m_State = State::Succeeded;
                }
            }
            break;

        case State::Succeeded:
        case State::Failed:
            m_CompletionFlag->store(true, std::memory_order_release);
            m_State = State::Completed;
            break;

        case State::Completed:
            break;
        default:
            MLG_ERROR("Invalid state: {}", static_cast<int>(m_State));
            break;
    }
}

Result<>
TextureLoadTask::Stage()
{
    MLG_DEBUG("Staging texture...");

    int width = 0, height = 0, numChannels = 0;

    if(!stbi_info_from_memory(m_Request.GetData().data(),
           narrow_cast<int>(m_Request.GetData().size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", m_Uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto texture = m_GpuHelper->CreateTexture(static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        m_Uri);

    MLG_CHECK(texture);

    auto stagingBuffer = m_GpuHelper->CreateStagingBuffer(*texture, m_Uri);
    MLG_CHECK(stagingBuffer);

    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    // It appears that mapping/unmapping must be done on the same thread
    // as other wgpu::Device operations.  Learned that the hard way by trying to map
    // in the worker thread below.
    m_Texture = *texture;
    m_StagingBuffer = *stagingBuffer;
    m_MappedMemory = static_cast<std::byte*>(mapped);

    MLG_CHECK(m_ThreadPool->Enqueue(TextureLoadTask::Decode, this),
        "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureLoadTask::Decode() const
{
    MLG_DEBUG("Decoding...");

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;
    stbi_uc* data = stbi_load_from_memory(m_Request.GetData().data(),
        narrow_cast<int>(m_Request.GetData().size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        GpuHelper::kNumTextureChannels);

    MLG_CHECKV(data, "Failed to decode image - {}", stbi_failure_reason());

    MLG_DEFER
    {
        stbi_image_free(data);
    };

    MLG_CHECKV(m_Texture.GetWidth() == static_cast<uint32_t>(imgWidth)
            && m_Texture.GetHeight() == static_cast<uint32_t>(imgHeight),
        "Decoded image dimensions do not match texture dimensions");

    MLG_CHECKV(m_Texture.GetFormat() == wgpu::TextureFormat::RGBA8Unorm,
        "Texture format does not match expected format");

    const size_t sizeofSrcData = static_cast<size_t>(imgWidth)
        * static_cast<size_t>(imgHeight)
        * GpuHelper::kNumTextureChannels;

    const size_t expectedSizeofSrcData = static_cast<size_t>(m_Texture.GetWidth())
        * static_cast<size_t>(m_Texture.GetHeight())
        * GpuHelper::kNumTextureChannels;

    MLG_CHECKV(sizeofSrcData == expectedSizeofSrcData,
        "Decoded image size does not match texture size");

    const std::span<const stbi_uc> srcSpan(data, sizeofSrcData);
    const std::span<std::byte> dstSpan(m_MappedMemory,
        narrow_cast<size_t>(m_StagingBuffer.GetSize()));
    size_t dstOffset = 0, srcOffset = 0;
    const size_t srcRowStride = static_cast<size_t>(imgWidth) * GpuHelper::kNumTextureChannels;
    const size_t dstRowStride =
        GpuHelper::GetTextureAlignedRowStride(static_cast<size_t>(imgWidth));
    for(int y = 0; y < imgHeight; ++y, dstOffset += dstRowStride, srcOffset += srcRowStride)
    {
        ::memcpy(&dstSpan[dstOffset], &srcSpan[srcOffset], srcRowStride);
    }

    return Result<>::Ok;
}

class FetchTexturesTask
{
public:
    FetchTexturesTask(GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        TextureCache& textureCache,
        std::filesystem::path basePath,
        const std::span<const MaterialDef>& materialDefs)
        : m_GpuHelper(&gpuHelper),
          m_ThreadPool(&threadPool),
          m_FileFetcher(&fileFetcher),
          m_TextureCache(&textureCache),
          m_BasePath(std::move(basePath)),
          m_MaterialDefs(materialDefs),
          m_CompletionFlags(materialDefs.size())
    {
        m_TaskHeap.reserve(materialDefs.size());
        m_Tasks.reserve(materialDefs.size());
    }

    Result<> Begin();

    Result<> Update();

    bool IsComplete() const { return m_State == State::Succeeded || m_State == State::Failed; }

private:
    enum class State
    {
        None,
        Fetching,
        Succeeded,
        Failed,
    };

    GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    TextureCache* m_TextureCache{ nullptr };
    std::filesystem::path m_BasePath;
    std::span<const MaterialDef> m_MaterialDefs;
    std::vector<TextureLoadTask> m_TaskHeap;
    std::vector<TextureLoadTask*> m_Tasks;
    wgpu::CommandEncoder m_Encoder{ nullptr };
    std::vector<std::atomic<bool>> m_CompletionFlags;

    State m_State{ State::None };
};

Result<>
FetchTexturesTask::Begin()
{
    MLG_CHECKV(m_State == State::None, "FetchTexturesTask has already been started");

    m_Encoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECKV(m_Encoder, "Failed to create command encoder");

    for(const auto& mtl : m_MaterialDefs)
    {
        if(mtl.BaseTextureUri.empty())
        {
            // No base texture for this material, skip it.
            continue;
        }

        if(m_TextureCache->Contains(mtl.BaseTextureUri))
        {
            // We've already loaded this texture, skip it.
            continue;
        }

        MLG_LOG_SCOPE(mtl.BaseTextureUri);

        const size_t index = m_TaskHeap.size();

        MLG_DEBUG("Fetching texture...");

        // Prepopulate the cache with the default texture.  If texture loading fails
        // then the default texture will be used instead of the missing texture.
        m_TextureCache->AddOrReplace(mtl.BaseTextureUri, m_GpuHelper->GetDefaultTexture());

        TextureLoadTask& task = m_TaskHeap.emplace_back(m_BasePath,
            mtl.BaseTextureUri,
            *m_GpuHelper,
            *m_FileFetcher,
            *m_ThreadPool,
            *m_TextureCache,
            m_Encoder,
            &m_CompletionFlags[index]);

        m_Tasks.emplace_back(&task);
    }

    m_State = State::Fetching;

    return Result<>::Ok;
}

Result<>
FetchTexturesTask::Update()
{
    switch(m_State)
    {
        case State::None:
            MLG_ERROR("FetchTexturesTask has not been started");
            return Result<>::Fail;

        case State::Fetching:
            m_FileFetcher->ProcessCompletions();

            for(size_t i = 0; i < m_Tasks.size();)
            {
                TextureLoadTask* tlTask = m_Tasks[i];

                tlTask->Update();

                if(tlTask->IsComplete())
                {
                    // Remove from the list.
                    m_Tasks[i] = std::move(m_Tasks.back());
                    m_Tasks.pop_back();
                }
                else
                {
                    ++i;
                }
            }

            if(m_Tasks.empty())
            {
                const wgpu::CommandBuffer commandBuffer = m_Encoder.Finish();
                m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);

                m_State = State::Succeeded;
            }
            break;

        case State::Succeeded:
        case State::Failed:
            break;

        default:
            MLG_ERROR("Invalid state: {}", static_cast<int>(m_State));
            return Result<>::Fail;
    }

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
    FetchTexturesTask fetchTask(gpuHelper,
        threadPool,
        fileFetcher,
        textureCache,
        basePath,
        materialDefs);

    MLG_CHECK(fetchTask.Begin());

    while(!fetchTask.IsComplete())
    {
        MLG_CHECK(fetchTask.Update());
    }

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

        materialConstants.emplace_back(mc);
    }

    auto buffer =
        gpuHelper.CreateStorageBuffer<GpuMaterialConstantsBuffer>(materialConstants.size(),
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

PropKit::PropKit(GpuVertexBuffer&& vertexBuffer,
    GpuIndexBuffer&& indexBuffer,
    GpuMaterialConstantsBuffer&& materialConstants,
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