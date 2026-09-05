#include "TextureFetcher.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "scope_exit.h"
#include "ThreadPool.h"

#include <stb_image.h>
#include <string>
#include <webgpu/webgpu_cpp.h>

class TextureFetcher::Impl
{
public:
    Impl(const GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        std::filesystem::path basePath,
        std::vector<std::string> textureUris);

    Impl() = delete;
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    Result<> Begin();

    void Update();

    bool IsComplete() const;

    bool Succeeded() const;

    Result<std::vector<wgpu::Texture>> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Succeeded,
        Failed,
    };    
    
    class LoadTask;

    struct PendingTask
    {
        LoadTask* Task;
        size_t Index;
    };

    const GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    std::filesystem::path m_BasePath;
    std::vector<std::string> m_TextureUris;
    std::vector<std::unique_ptr<LoadTask>> m_TaskHeap;
    std::vector<PendingTask> m_Tasks;
    std::vector<wgpu::Texture> m_Textures;
    wgpu::CommandEncoder m_CmdEncoder;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{false};
};

class TextureFetcher::Impl::LoadTask
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

    LoadTask(const std::filesystem::path& basePath,
        std::string baseUri,
        const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        wgpu::CommandEncoder encoder);

    LoadTask() = delete;
    ~LoadTask() = default;
    LoadTask(const LoadTask&) = delete;
    LoadTask& operator=(const LoadTask&) = delete;
    LoadTask(LoadTask&&) = delete;
    LoadTask& operator=(LoadTask&&) = delete;

    void Update();

    bool IsComplete() const { return m_Stage == Stage::Succeeded || m_Stage == Stage::Failed; }

    bool Succeeded() const
    {
        MLG_ASSERT(IsComplete());
        return m_Stage == Stage::Succeeded;
    }

    const std::string& GetUri() const { return m_Uri; }

    wgpu::Texture Take()
    {
        MLG_ASSERT(Succeeded(), "Task did not succeed");
        MLG_ASSERT(m_Texture, "Texture is not valid");

        wgpu::Texture texture = m_Texture;
        m_Texture = {};

        return texture;
    }

private:
    Result<> BeginDecode();

    Result<> Decode() const;

    static void Decode(void* userData)
    {
        LoadTask* task = static_cast<LoadTask*>(userData);
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

TextureFetcher::Impl::LoadTask::LoadTask(const std::filesystem::path& basePath,
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
TextureFetcher::Impl::LoadTask::Update()
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
TextureFetcher::Impl::LoadTask::BeginDecode()
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

    MLG_CHECK(m_ThreadPool->Enqueue(LoadTask::Decode, this),
        "Failed to enqueue texture decode task");

    return Result<>::Ok;
}

Result<>
TextureFetcher::Impl::LoadTask::Decode() const
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
TextureFetcher::Impl::LoadTask::SetSucceeded()
{
    m_Stage = Stage::Succeeded;
}

void
TextureFetcher::Impl::LoadTask::SetFailed()
{
    m_Stage = Stage::Failed;
}

/// TextureFetcher::Impl

TextureFetcher::Impl::Impl(const GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    std::filesystem::path basePath,
    std::vector<std::string> textureUris)
    : m_GpuHelper(&gpuHelper),
      m_ThreadPool(&threadPool),
      m_FileFetcher(&fileFetcher),
      m_BasePath(std::move(basePath)),
      m_TextureUris(std::move(textureUris))
{
    m_TaskHeap.reserve(m_TextureUris.size());
    m_Tasks.reserve(m_TextureUris.size());
    m_Textures.reserve(m_TextureUris.size());
}

TextureFetcher::Impl::~Impl()
{
    MLG_ASSERT(m_Tasks.empty());
}

void
TextureFetcher::Impl::Update()
{
    if(!MLG_VERIFY(!IsComplete(), "Task is already complete"))
    {
        return;
    }

    if(!MLG_VERIFY(Stage::None != m_Stage, "Task is not started"))
    {
        return;
    }

    switch(m_Stage)
    {
        case Stage::Fetching:
            m_FileFetcher->ProcessCompletions();

            for(size_t i = 0; i < m_Tasks.size();)
            {
                LoadTask* tlTask = m_Tasks[i].Task;

                tlTask->Update();

                if(tlTask->IsComplete())
                {
                    if(tlTask->Succeeded())
                    {
                        // Populate the texture in the corresponding slot.
                        m_Textures[m_Tasks[i].Index] = tlTask->Take();
                    }

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
                // We're done.

                // Submit the command buffer that was used by all the tasks.
                const wgpu::CommandBuffer commandBuffer = m_CmdEncoder.Finish();
                m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);
                m_CmdEncoder = {};

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
TextureFetcher::Impl::IsComplete() const
{
    return MLG_VERIFY(m_Stage != Stage::None, "Task is not started")
        && (Stage::Succeeded == m_Stage || Stage::Failed == m_Stage);
}

bool
TextureFetcher::Impl::Succeeded() const
{
    MLG_ASSERT(IsComplete(), "Task is not complete");
    return m_Stage == Stage::Succeeded;
}

Result<std::vector<wgpu::Texture>>
TextureFetcher::Impl::Take()
{
    MLG_CHECKV(IsComplete(), "Task is not complete");
    MLG_CHECKV(Succeeded(), "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;
    return std::move(m_Textures);
}

// private:

Result<>
TextureFetcher::Impl::Begin()
{
    MLG_CHECKV(m_Stage == Stage::None);

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    MLG_CHECKV(!m_TextureUris.empty(), "No texture URIs provided");

    m_CmdEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECKV(m_CmdEncoder, "Failed to create command encoder");

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

        LoadTask& task =
            *m_TaskHeap.emplace_back(std::make_unique<LoadTask>(m_BasePath,
                uri,
                *m_GpuHelper,
                *m_FileFetcher,
                *m_ThreadPool,
                m_CmdEncoder));

        m_Tasks.emplace_back(&task, m_Textures.size() - 1);
    }

    if(m_Tasks.empty())
    {
        // Nothing to do
        m_Stage = Stage::Succeeded;
    }
    else
    {
        m_Stage = Stage::Fetching;
    }

    return Result<>::Ok;
}

/// TextureFetcher

// These need to know details of TextureFetcher:Impl, so they are defined in the .cpp file.
TextureFetcher::~TextureFetcher() = default;
TextureFetcher::TextureFetcher(TextureFetcher&&) noexcept = default;
TextureFetcher& TextureFetcher::operator=(TextureFetcher&&) noexcept = default;

Result<TextureFetcher>
TextureFetcher::Create(const GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    std::filesystem::path basePath,
    std::vector<std::string> textureUris)
{
    std::unique_ptr<TextureFetcher::Impl> impl = std::make_unique<TextureFetcher::Impl>(gpuHelper,
        threadPool,
        fileFetcher,
        std::move(basePath),
        std::move(textureUris));

    MLG_CHECK(impl->Begin());

    return TextureFetcher(std::move(impl));
}

TextureFetcher::TextureFetcher(std::unique_ptr<Impl> impl)
    : m_Impl(std::move(impl))
{
}

void
TextureFetcher::Update()
{
    MLG_ASSERT(IsValid(), "Invalid Task");

    m_Impl->Update();
}

bool
TextureFetcher::IsComplete() const
{
    MLG_ASSERT(IsValid(), "Invalid Task");

    return m_Impl->IsComplete();
}

bool
TextureFetcher::Succeeded() const
{
    MLG_ASSERT(IsValid(), "Invalid Task");
    MLG_ASSERT(IsComplete(), "Task is not complete");

    return m_Impl->Succeeded();
}

Result<std::vector<wgpu::Texture>>
TextureFetcher::Take()
{
    MLG_CHECKV(IsValid(), "Invalid Task");

    std::unique_ptr bye = std::move(m_Impl);
    return bye->Take();
}

bool
TextureFetcher::IsValid() const
{
    return m_Impl != nullptr;
}