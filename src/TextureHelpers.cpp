#include "TextureHelpers.h"


#include "Defer.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "System.h"
#include "ThreadPool.h"

#include <atomic>
#include <limits>
#include <stb_image.h>
#include <webgpu/webgpu_cpp.h>

TextureFetchTask::TextureFetchTask(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const FilePath& path,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Path(path),
      m_CommandEncoder(std::move(commandEncoder))
{
}

Result<wgpu::Texture>
TextureFetchTask::Take()
{
    MLG_CHECKV(!IsRunning(), "Task is not complete");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task failed");
    MLG_CHECKV(m_Texture, "Texture is not valid");

    wgpu::Texture texture = m_Texture;
    m_Texture = nullptr; // Invalidate the texture so it can only be taken once

    return texture;
}

// private:

Result<>
TextureFetchTask::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task already started");

    m_Stage = Stage::Failed; // Set to failed in case of early exit

    m_Texture = m_GpuHelper->GetDefaultTexture();
    MLG_CHECK(m_Texture, "Failed to get default texture");

    auto fetchRequestId = m_FileFetcher->Fetch(m_Path);
    MLG_CHECK(fetchRequestId);

    m_FetchRequestId = *fetchRequestId;

    m_Stage = Stage::Fetching;

    return Result<>::Ok;
}

void
TextureFetchTask::OnUpdate()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Fetching:
            if(!m_FileFetcher->IsPending(m_FetchRequestId))
            {
                auto fetchedData = m_FileFetcher->Take(m_FetchRequestId);

                if(!fetchedData)
                {
                    MLG_ERROR("Failed to take fetched data");
                    m_Stage = Stage::Failed;
                }
                else if(BeginDecode(std::move(*fetchedData)))
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

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            m_CompletionFlag.store(true, std::memory_order_release);
            break;
    }
}

Result<>
TextureFetchTask::BeginDecode(std::vector<std::byte>&& fetchedData)
{
    MLG_DEBUG("Staging texture...");

    m_FetchedData = std::move(fetchedData);

    int width = 0, height = 0, numChannels = 0;

    const void* p = m_FetchedData.data();
    const stbi_uc* fetchedBytes = static_cast<const stbi_uc*>(p);

    if(!stbi_info_from_memory(fetchedBytes,
           static_cast<int>(m_FetchedData.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto texture = m_GpuHelper->CreateTexture(static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        m_Path.GetStem());

    MLG_CHECK(texture);

    auto stagingBuffer = m_GpuHelper->CreateStagingBuffer(*texture, m_Path.GetStem());
    MLG_CHECK(stagingBuffer);
    MLG_CHECKV(stagingBuffer->GetSize() > 0, "Staging buffer has zero size");
    MLG_CHECKV(stagingBuffer->GetSize() <= std::numeric_limits<size_t>::max(),
        "Staging buffer size exceeds maximum allowed size");

    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    m_Texture = *texture;
    m_StagingBuffer = *stagingBuffer;
    m_MappedMemory = std::span<std::byte>(static_cast<std::byte*>(mapped),
        static_cast<size_t>(stagingBuffer->GetSize()));

    // webgpu objects must be accessed only on the main thread, so we cache
    // values needed by the worker thread during decoding.
    m_ExpectedTexWidth = static_cast<uint32_t>(width);
    m_ExpectedTexHeight = static_cast<uint32_t>(height);

    MLG_CHECK(m_Texture.GetFormat() == GpuHelper::kTextureFormat,
        "GPU texture format does not match expected format");

    MLG_CHECK(m_ThreadPool->Enqueue(Decode, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureFetchTask::Decode()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    MLG_DEBUG("Decoding...");

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;

    const void* p = m_FetchedData.data();
    const stbi_uc* fetchedBytes = static_cast<const stbi_uc*>(p);

    stbi_uc* data = stbi_load_from_memory(fetchedBytes,
        static_cast<int>(m_FetchedData.size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        GpuHelper::kNumTextureChannels);

    // Free the fetched data to save memory.
    std::vector<std::byte>().swap(m_FetchedData);

    MLG_CHECKV(data, "Failed to decode image - {}", stbi_failure_reason());

    MLG_DEFER
    {
        stbi_image_free(data);
    };

    MLG_CHECKV(std::cmp_equal(m_ExpectedTexWidth, imgWidth)
            && std::cmp_equal(m_ExpectedTexHeight, imgHeight),
        "Decoded image dimensions do not match texture dimensions");

    const size_t sizeofSrcData = static_cast<size_t>(imgWidth)
        * static_cast<size_t>(imgHeight)
        * GpuHelper::kNumTextureChannels;

    const size_t expectedSizeofSrcData = static_cast<size_t>(m_ExpectedTexWidth)
        * static_cast<size_t>(m_ExpectedTexHeight)
        * GpuHelper::kNumTextureChannels;

    MLG_CHECKV(sizeofSrcData == expectedSizeofSrcData,
        "Decoded image size does not match texture size");

    const std::span<const stbi_uc> srcSpan(data, sizeofSrcData);
    size_t dstOffset = 0, srcOffset = 0;
    const size_t srcRowStride = static_cast<size_t>(imgWidth) * GpuHelper::kNumTextureChannels;
    const size_t dstRowStride =
        GpuHelper::GetTextureAlignedRowStride(static_cast<size_t>(imgWidth));
    for(int y = 0; y < imgHeight; ++y, dstOffset += dstRowStride, srcOffset += srcRowStride)
    {
        ::memcpy(&m_MappedMemory[dstOffset], &srcSpan[srcOffset], srcRowStride);
    }

    return Result<>::Ok;
}

void
TextureFetchTask::Decode(void* userData)
{
    TextureFetchTask* task = static_cast<TextureFetchTask*>(userData);
    task->m_DecodeResult = task->Decode();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}

Result<>
TextureFetchTask::CommitStagingBuffer()
{
    MLG_DEBUG("Committing staging buffer...");

    MLG_CHECK(GpuHelper::CommitStagingBuffer(m_Texture, m_StagingBuffer, m_CommandEncoder),
        "Failed to commit staging buffer");

    return Result<>::Ok;
}