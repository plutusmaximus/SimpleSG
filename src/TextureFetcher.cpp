#define MLG_LOGGER_NAME "TEXF"

#include "TextureFetcher.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "scope_exit.h"
#include "System.h"
#include "ThreadPool.h"

#include <atomic>
#include <ranges>
#include <stb_image.h>
#include <string>
#include <webgpu/webgpu_cpp.h>

TextureFetcher::FetchTask::FetchTask(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const std::filesystem::path& basePath,
    std::string baseUri,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Uri(std::move(baseUri)),
      m_FullPath((basePath / m_Uri).string()),
      m_Texture(gpuHelper.GetDefaultTexture()),
      m_CommandEncoder(std::move(commandEncoder))
{
}

TextureFetcher::FetchTask::~FetchTask()
{
    MLG_ASSERT(Stage::None == m_Stage || !FetchTask::IsPending(), "Destroying pending task");
}

Result<>
TextureFetcher::FetchTask::Begin()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task already started");

    if(m_Uri.empty())
    {
        MLG_ERROR("URI is empty");

        // It's ok - we logged the problem and we'll use the default texture.
        m_Stage = Stage::Succeeded;
        return Result<>::Ok;
    }

    m_Stage = Stage::Failed; // Set to failed in case of early exit

    auto fetchRequestId = m_FileFetcher->Fetch(m_FullPath);
    MLG_CHECK(fetchRequestId);

    m_FetchRequestId = *fetchRequestId;

    m_Stage = Stage::Fetching;

    return Result<>::Ok;
}

void
TextureFetcher::FetchTask::Update()
{
    MLG_LOG_SCOPE(m_Uri);

    if(!MLG_VERIFY(IsPending(), "Task is not running"))
    {
        return;
    }

    switch(m_Stage)
    {
        case Stage::None:
            break;
        case Stage::Fetching:
            if(!m_FileFetcher->IsPending(m_FetchRequestId))
            {
                if(!m_FileFetcher->Take(m_FetchRequestId, m_FetchedData))
                {
                    MLG_ERROR("Failed to take fetched data");
                    m_Stage = Stage::Failed;
                }
                else if(BeginDecode())
                {
                    m_Stage = Stage::Decoding;
                }
                else
                {
                    MLG_ERROR("Failed to stage texture");
                    m_Stage = Stage::Failed;
                }
            }
            break;
        case Stage::Decoding:
            if(m_CompletionFlag.load(std::memory_order_acquire))
            {
                if(!m_DecodeResult)
                {
                    MLG_ERROR("Failed to decode texture");
                    m_Stage = Stage::Failed;
                }
                else if(!CommitStagingBuffer())
                {
                    m_Stage = Stage::Failed;
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    m_Stage = Stage::Succeeded;
                }
            }
            break;

        case Stage::Succeeded:
        case Stage::Failed:
            m_CompletionFlag.store(true, std::memory_order_release);
            break;
        default:
            MLG_ERROR("Invalid stage: {}", static_cast<int>(m_Stage));
            break;
    }
}

bool
TextureFetcher::FetchTask::IsPending() const
{
    return MLG_VERIFY(Stage::None != m_Stage, "Task is not started")
        && Stage::Succeeded != m_Stage
        && Stage::Failed != m_Stage;
}

Result<wgpu::Texture>
TextureFetcher::FetchTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task is not complete");
    MLG_CHECKV(m_Texture, "Texture is not valid");

    wgpu::Texture texture = m_Texture;
    m_Texture = nullptr; // Invalidate the texture so it can only be taken once

    return texture;
}

Result<>
TextureFetcher::FetchTask::BeginDecode()
{
    MLG_DEBUG("Staging texture...");

    int width = 0, height = 0, numChannels = 0;

    if(!stbi_info_from_memory(m_FetchedData.data(),
           static_cast<int>(m_FetchedData.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}/{}", m_Uri, stbi_failure_reason());
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

    MLG_CHECK(m_ThreadPool->Enqueue(Decode, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureFetcher::FetchTask::Decode() const
{
    MLG_DEBUG("Decoding...");

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;
    stbi_uc* data = stbi_load_from_memory(m_FetchedData.data(),
        static_cast<int>(m_FetchedData.size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        GpuHelper::kNumTextureChannels);

    MLG_CHECKV(data, "Failed to decode image - {}", stbi_failure_reason());

    MLG_DEFER
    {
        stbi_image_free(data);
    };

    MLG_CHECKV(std::cmp_equal(m_Texture.GetWidth(), imgWidth)
            && std::cmp_equal(m_Texture.GetHeight(), imgHeight),
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
        static_cast<size_t>(m_StagingBuffer.GetSize()));
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

void
TextureFetcher::FetchTask::Decode(void* userData)
{
    FetchTask* task = static_cast<FetchTask*>(userData);
    task->m_DecodeResult = task->Decode();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}

Result<>
TextureFetcher::FetchTask::CommitStagingBuffer()
{
    MLG_DEBUG("Committing staging buffer...");

    MLG_CHECK(GpuHelper::CommitStagingBuffer(m_Texture, m_StagingBuffer, m_CommandEncoder),
        "Failed to commit staging buffer");

    return Result<>::Ok;
}

/// TextureFetcher

TextureFetcher::TextureFetcher(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    std::filesystem::path basePath,
    std::vector<std::string> textureUris)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_BasePath(std::move(basePath)),
      m_TextureUris(std::move(textureUris))
{
    m_Textures.reserve(m_TextureUris.size());
}

TextureFetcher::~TextureFetcher()
{
    MLG_ASSERT(Stage::None == m_Stage || !TextureFetcher::IsPending(), "Destroying pending task");
}

Result<>
TextureFetcher::Begin()
{
    MLG_CHECKV(m_Stage == Stage::None, "Task is already in progress");

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    MLG_CHECKV(!m_TextureUris.empty(), "No texture URIs provided");

    m_CommandEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECKV(m_CommandEncoder, "Failed to create command encoder");

    std::vector<ICoopTask*> taskBatch;
    taskBatch.reserve(m_TextureUris.size());

    for(const std::string& uri : m_TextureUris)
    {
        m_Textures.push_back(m_GpuHelper->GetDefaultTexture());

        MLG_LOG_SCOPE(uri);

        MLG_DEBUG("Fetching texture...");

        FetchTask& task = m_Tasks.emplace_back(*m_GpuHelper,
            *m_FileFetcher,
            *m_ThreadPool,
            m_BasePath,
            uri,
            m_CommandEncoder);

        taskBatch.push_back(&task);
    }

    m_TaskBatch.emplace(std::move(taskBatch));

    MLG_CHECK(m_TaskBatch->Begin());

    m_Stage = Stage::Fetching;

    return Result<>::Ok;
}

void
TextureFetcher::Update()
{
    if(!MLG_VERIFY(IsPending(), "Task is not running"))
    {
        return;
    }

    MLG_ABORTIF(!m_TaskBatch, "Task batch is not initialized");

    switch(m_Stage)
    {
        case Stage::Fetching:
            if(m_TaskBatch->IsPending())
            {
                m_TaskBatch->Update();
            }
            else
            {
                const wgpu::CommandBuffer commandBuffer = m_CommandEncoder.Finish();
                m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);

                for(auto [task, texture] : std::views::zip(m_Tasks, m_Textures))
                {
                    auto result = task.Take();
                    if(result)
                    {
                        texture = std::move(*result);
                    }
                }

                m_Stage = Stage::Succeeded;
            }
            break;

        case Stage::Succeeded:
        case Stage::Failed:
            break;

        default:
            MLG_ABORT("Invalid stage: {}", static_cast<int>(m_Stage));
            return;
    }
}

bool
TextureFetcher::IsPending() const
{
    return MLG_VERIFY(Stage::None != m_Stage, "Task is not started")
        && Stage::Succeeded != m_Stage
        && Stage::Failed != m_Stage;
}

Result<std::vector<wgpu::Texture>>
TextureFetcher::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;
    return std::move(m_Textures);
}