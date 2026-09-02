#include "TextureFetcher.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "scope_exit.h"
#include "ThreadPool.h"

#include <ranges>
#include <stb_image.h>
#include <string>
#include <webgpu/webgpu_cpp.h>

namespace detail
{
class TextureLoadTask
{
public:
    enum class Stage
    {
        None,
        Fetch,
        Fetching,
        Decoding,
        Succeeded,
        Failed
    };

    TextureLoadTask(const std::filesystem::path& basePath,
        std::string baseUri,
        const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        wgpu::CommandEncoder encoder);

    TextureLoadTask() = delete;
    ~TextureLoadTask() = default;
    TextureLoadTask(const TextureLoadTask&) = delete;
    TextureLoadTask& operator=(const TextureLoadTask&) = delete;
    TextureLoadTask(TextureLoadTask&&) = delete;
    TextureLoadTask& operator=(TextureLoadTask&&) = delete;

    void Update();

    bool IsComplete() const { return m_Stage == Stage::Succeeded || m_Stage == Stage::Failed; }

    bool Succeeded() const
    {
        MLG_ASSERT(IsComplete());
        return m_Stage == Stage::Succeeded;
    }

    const std::string& GetUri() const { return m_Uri; }

    wgpu::Texture& GetTexture() { return m_Texture; }

private:

    Result<> BeginDecode();

    Result<> Decode() const;

    static void Decode(void* userData)
    {
        TextureLoadTask* task = static_cast<TextureLoadTask*>(userData);
        task->m_DecodeResult = task->Decode();
        task->m_CompletionFlag.store(true, std::memory_order_release);
    }

    void SetSucceeded();

    void SetFailed();

    friend TextureFetcher;

    std::string m_Uri;
    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    wgpu::CommandEncoder m_Encoder{ nullptr };
    FileFetcher::Request m_Request;
    wgpu::Texture m_Texture;
    wgpu::Buffer m_StagingBuffer;
    std::byte* m_MappedMemory{ nullptr };
    Result<> m_DecodeResult;

    std::atomic<bool> m_CompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};

TextureLoadTask::TextureLoadTask(const std::filesystem::path& basePath,
    std::string baseUri,
    const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    wgpu::CommandEncoder encoder)
    : m_Uri(std::move(baseUri)),
      m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Encoder(std::move(encoder)),
      m_Request(basePath / m_Uri),
      m_Stage(Stage::Fetch)
{
}

void
TextureLoadTask::Update()
{
    MLG_LOG_SCOPE(m_Uri);

    switch(m_Stage)
    {
        case Stage::None:
            break;
        case Stage::Fetch:
            if(m_FileFetcher->Fetch(m_Request))
            {
                m_Stage = Stage::Fetching;
            }
            else
            {
                MLG_ERROR("Failed to fetch texture");
                SetFailed();
            }
            break;
        case Stage::Fetching:
            if(m_Request.Succeeded())
            {
                if(BeginDecode())
                {
                    m_Stage = Stage::Decoding;
                }
                else
                {
                    MLG_ERROR("Failed to stage texture");
                    SetFailed();
                }
            }
            else if(!m_Request.IsPending())
            {
                MLG_ERROR("Failed to fetch texture");
                SetFailed();
            }
            break;
        case Stage::Decoding:
            if(m_CompletionFlag.load(std::memory_order_acquire))
            {
                if(!m_DecodeResult)
                {
                    MLG_ERROR("Failed to decode texture");
                    SetFailed();
                }
                else if(!GpuHelper::CommitStagingBuffer(m_Texture, m_StagingBuffer, m_Encoder))
                {
                    MLG_ERROR("Failed to commit texture");
                    SetFailed();
                }
                else
                {
                    MLG_DEBUG("Loaded");
                    SetSucceeded();
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

Result<>
TextureLoadTask::BeginDecode()
{
    MLG_DEBUG("Staging texture...");

    int width = 0, height = 0, numChannels = 0;

    if(!stbi_info_from_memory(m_Request.GetData().data(),
           static_cast<int>(m_Request.GetData().size()),
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
        static_cast<int>(m_Request.GetData().size()),
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
TextureLoadTask::SetSucceeded()
{
    m_Stage = Stage::Succeeded;
}

void
TextureLoadTask::SetFailed()
{
    m_Stage = Stage::Failed;
}
} // namespace detail

TextureFetcher::TextureFetcher(const GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    std::filesystem::path basePath,
    const std::span<const std::string_view>& textureUris)
    : m_GpuHelper(&gpuHelper),
        m_ThreadPool(&threadPool),
        m_FileFetcher(&fileFetcher),
        m_BasePath(std::move(basePath))
{
    m_TextureUris.reserve(textureUris.size());
    m_TextureUris.assign(textureUris.begin(), textureUris.end());
    m_TaskHeap.reserve(textureUris.size());
    m_Tasks.reserve(textureUris.size());
    m_Textures.reserve(textureUris.size());
}

// This is here to avoid the compiler generating a default destructor that doesn't
// know how to destruct std::vector<detail::TextureLoadTask*> properly, because detail::TextureLoadTask
// is forward-declared in the header and other translation units do not have the full definition.
// Esoteric C++ bullsh*t.  Comment out this dtor and see what happens.
TextureFetcher::~TextureFetcher() = default;

void
TextureFetcher::Update()
{
    switch(m_Stage)
    {
        case Stage::Begin:
            if(Begin())
            {
                m_Stage = Stage::Fetching;
            }
            else
            {
                m_Stage = Stage::Failed;
            }
            break;

        case Stage::Fetching:
            m_FileFetcher->ProcessCompletions();

            for(size_t i = 0; i < m_Tasks.size();)
            {
                detail::TextureLoadTask* tlTask = m_Tasks[i];

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
                if(!m_TaskHeap.empty())
                {
                    const wgpu::CommandEncoder encoder = m_TaskHeap.front()->m_Encoder;
                    const wgpu::CommandBuffer commandBuffer = encoder.Finish();
                    m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);
                }

                const auto view = std::views::zip(m_TaskHeap, m_Textures);
                for(auto [task, texture] : view)
                {
                    if(task->Succeeded())
                    {
                        texture = task->GetTexture();
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

std::span<const wgpu::Texture>
TextureFetcher::GetTextures() const
{
    MLG_ASSERT(IsComplete());

    return m_Textures;
}

// private:

Result<>
TextureFetcher::Begin()
{
    MLG_CHECKV(m_Stage == Stage::Begin);

    const wgpu::CommandEncoder encoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECKV(encoder, "Failed to create command encoder");

    for(const std::string& uri : m_TextureUris)
    {
        m_Textures.push_back(m_GpuHelper->GetDefaultTexture());

        if(uri.empty())
        {
            // No texture to load
            continue;
        }

        MLG_LOG_SCOPE(uri);

        MLG_DEBUG("Fetching texture...");

        detail::TextureLoadTask& task = *m_TaskHeap.emplace_back(std::make_unique<detail::TextureLoadTask>(
            m_BasePath,
            uri,
            *m_GpuHelper,
            *m_FileFetcher,
            *m_ThreadPool,
            encoder));

        m_Tasks.push_back(&task);
    }

    return Result<>::Ok;
}