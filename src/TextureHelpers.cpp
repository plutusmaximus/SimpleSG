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

/// MlgTextureHeader

bool
MlgTextureHeader::IsMlgTexture() const
{
    return Magic0 == kMagic0 && Magic1 == kMagic1 && Version == kVersion;
}

const MlgTextureHeader*
MlgTextureHeader::GetHeader(const std::span<const std::byte>& buffer)
{
    if(buffer.size() < sizeof(MlgTextureHeader))
    {
        return nullptr;
    }

    const void* p = buffer.data();
    return static_cast<const MlgTextureHeader*>(p);
}

Result<>
MlgTextureHeader::Validate(const std::span<const std::byte> buffer)
{
    const MlgTextureHeader* header = GetHeader(buffer);
    MLG_CHECKV(header, "Invalid texture header");

    MLG_CHECKV(header->Width > 0, "Image width must be greater than zero");
    MLG_CHECKV(header->Height > 0, "Image height must be greater than zero");

    MLG_CHECKV(header->Width <= MlgTextureHeader::kMaxImageWidth,
        "Image width exceeds maximum width: {} > {}",
        header->Width,
        MlgTextureHeader::kMaxImageWidth);

    MLG_CHECKV(header->Height <= MlgTextureHeader::kMaxImageHeight,
        "Image height exceeds maximum height: {} > {}",
        header->Height,
        MlgTextureHeader::kMaxImageHeight);

    // Verify the source data has been stride-aligned.
    const size_t dstRowStride =
        GpuHelper::GetTextureAlignedRowStride(static_cast<size_t>(header->Width));

    MLG_CHECKV(dstRowStride <= buffer.size(),
        "Destination row stride exceeds size of source data: {} > {}",
        dstRowStride,
        buffer.size());

    MLG_CHECKV(buffer.size() / dstRowStride == static_cast<size_t>(header->Height),
        "Source data is not properly stride-aligned");

    const size_t expectedSizeofImgData = static_cast<size_t>(header->Height) * dstRowStride;

    MLG_CHECKV(expectedSizeofImgData == buffer.size() - sizeof(MlgTextureHeader),
        "Source image data size does not match expected size: {} != {}",
        buffer.size(),
        expectedSizeofImgData);

    return Result<>::Ok;
}

/// TextureDecodeTask

TextureDecodeTask::TextureDecodeTask(const GpuHelper& gpuHelper,
    std::vector<std::byte>&& rawData,
    const FilePath& path,
    ThreadPool& threadPool,
    wgpu::CommandEncoder commandEncoder)
    : m_GpuHelper(&gpuHelper),
      m_ThreadPool(&threadPool),
      m_Path(path),
      m_TexBytes(std::move(rawData)),
      m_CommandEncoder(std::move(commandEncoder))
{
}

Result<wgpu::Texture>
TextureDecodeTask::Take()
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
TextureDecodeTask::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task already started");

    m_Stage = Stage::Failed; // Set to failed in case of early exit

    // Default texture in case decoding fails
    m_Texture = m_GpuHelper->GetDefaultTexture();
    MLG_CHECK(m_Texture, "Failed to get default texture");

    auto stageResult = BeginDecode();
    MLG_CHECK(stageResult, "Failed to begin decode");

    m_Stage = Stage::Decoding;

    return Result<>::Ok;
}

void
TextureDecodeTask::OnUpdate()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
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
                    m_Texture = std::move(m_StagingTexture);
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
TextureDecodeTask::BeginDecode()
{
    MLG_DEBUG("Beginning texture decode...");

    const MlgTextureHeader* header = MlgTextureHeader::GetHeader(m_TexBytes);
    if(MLG_VERIFY(header) && header->IsMlgTexture())
    {
        MLG_CHECK(BeginDecodeMlg(), "Failed to begin mlg decode");
    }
    else
    {
        MLG_CHECK(BeginDecodeStb(), "Failed to begin stb decode");
    }

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::BeginDecodeMlg()
{
    MLG_DEBUG("Begin decoding mlg texture...");

    MLG_CHECK(MlgTextureHeader::Validate(m_TexBytes));

    const MlgTextureHeader* header = MlgTextureHeader::GetHeader(m_TexBytes);
    MLG_CHECK(header, "Failed to get texture header");

    static_assert(std::numeric_limits<uint32_t>::max() / MlgTextureHeader::kMaxImageWidth
            >= MlgTextureHeader::kMaxImageHeight,
        "kMaxImageWidth * kMaxImageHeight exceeds uint32_t max");

    MLG_DEBUG("Image info - {} x {} x {}",
        header->Width,
        header->Height,
        GpuHelper::kNumTextureChannels);

    m_ExpectedTexWidth = header->Width;
    m_ExpectedTexHeight = header->Height;

    MLG_CHECK(BeginStaging());

    MLG_CHECK(m_ThreadPool->Enqueue(DecodeMlg, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::BeginDecodeStb()
{
    MLG_DEBUG("Begin decoding stb texture...");

    int width = 0, height = 0, numChannels = 0;

    const void* p = m_TexBytes.data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    if(!stbi_info_from_memory(texBytes,
           static_cast<int>(m_TexBytes.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    m_ExpectedTexWidth = static_cast<uint32_t>(width);
    m_ExpectedTexHeight = static_cast<uint32_t>(height);

    MLG_CHECK(BeginStaging());

    MLG_CHECK(m_ThreadPool->Enqueue(DecodeStb, this), "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::BeginStaging()
{
    MLG_DEBUG("Staging texture...");

    auto textureResult =
        m_GpuHelper->CreateTexture(m_ExpectedTexWidth, m_ExpectedTexHeight, m_Path.GetStem());

    MLG_CHECK(textureResult);

    MLG_CHECK(textureResult->GetFormat() == GpuHelper::kTextureFormat,
        "GPU texture format does not match expected format");

    auto stagingBuffer = m_GpuHelper->CreateStagingBuffer(*textureResult, m_Path.GetStem());
    MLG_CHECK(stagingBuffer);
    MLG_CHECKV(stagingBuffer->GetSize() > 0, "Staging buffer has zero size");
    MLG_CHECKV(stagingBuffer->GetSize() <= std::numeric_limits<size_t>::max(),
        "Staging buffer size exceeds maximum allowed size");

    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    m_StagingTexture = *textureResult;
    m_StagingBuffer = *stagingBuffer;
    m_MappedMemory = std::span<std::byte>(static_cast<std::byte*>(mapped),
        static_cast<size_t>(stagingBuffer->GetSize()));

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::DecodeMlg()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    MLG_DEBUG("Decoding MLG texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_TexBytes);
    };

    MLG_CHECK(MlgTextureHeader::Validate(std::span(m_TexBytes)));

    const std::span srcSpan = std::span(m_TexBytes).subspan(sizeof(MlgTextureHeader));

    MLG_CHECKV(m_MappedMemory.size() >= srcSpan.size(),
        "Mapped memory is smaller than source span");

    ::memcpy(m_MappedMemory.data(), srcSpan.data(), srcSpan.size());

    return Result<>::Ok;
}

Result<>
TextureDecodeTask::DecodeStb()
{
    MLG_LOG_SCOPE(m_Path.GetStem());

    MLG_DEBUG("Decoding stb texture...");

    MLG_DEFER
    {
        // Free the buffer to reclaim memory.
        std::vector<std::byte>().swap(m_TexBytes);
    };

    int imgWidth = 0, imgHeight = 0, imgNumChannels = 0;

    const void* p = m_TexBytes.data();
    const stbi_uc* texBytes = static_cast<const stbi_uc*>(p);

    stbi_uc* data = stbi_load_from_memory(texBytes,
        static_cast<int>(m_TexBytes.size()),
        &imgWidth,
        &imgHeight,
        &imgNumChannels,
        GpuHelper::kNumTextureChannels);

    MLG_CHECKV(data, "Failed to decode image - {}", stbi_failure_reason());

    // Eager reclaiming of memory.  The deferred recalaim from above will
    // still run but it will be a no-op.
    std::vector<std::byte>().swap(m_TexBytes);

    MLG_DEFER
    {
        // Free the stb image buffer.
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
TextureDecodeTask::DecodeMlg(void* userData)
{
    TextureDecodeTask* task = static_cast<TextureDecodeTask*>(userData);
    task->m_DecodeResult = task->DecodeMlg();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}
void
TextureDecodeTask::DecodeStb(void* userData)
{
    TextureDecodeTask* task = static_cast<TextureDecodeTask*>(userData);
    task->m_DecodeResult = task->DecodeStb();
    task->m_CompletionFlag.store(true, std::memory_order_release);
}

Result<>
TextureDecodeTask::CommitStagingBuffer()
{
    MLG_DEBUG("Committing staging buffer...");

    MLG_CHECK(GpuHelper::CommitStagingBuffer(m_StagingTexture, m_StagingBuffer, m_CommandEncoder),
        "Failed to commit staging buffer");

    return Result<>::Ok;
}

/// TextureFetchTask

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

    // If loading fails then we use the default texture.
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
                else if(m_TextureDecodeTask.emplace(*m_GpuHelper,
                            std::move(*fetchedData),
                            m_Path,
                            *m_ThreadPool,
                            m_CommandEncoder);
                    !m_TextureDecodeTask->Start())
                {
                    MLG_ERROR("Failed to start texture decode task");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::Decoding;
                }
            }
            break;
        case Stage::Decoding:
            if(m_TextureDecodeTask->IsRunning())
            {
                m_TextureDecodeTask->Update();
            }
            else
            {
                if(auto textureResult = m_TextureDecodeTask->Take(); !textureResult)
                {
                    MLG_ERROR("Failed to take decoded texture");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    m_Texture = *textureResult;
                    m_Stage = Stage::Succeeded;
                }

                // Reclaim resources.
                m_TextureDecodeTask.reset();
            }
            break;

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}