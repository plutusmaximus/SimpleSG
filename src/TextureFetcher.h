#pragma once

#include "CoopTask.h"
#include "Result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class System;
class GpuHelper;
class FileFetcher;
class ThreadPool;

using FetchRequestId = uint64_t;

class TextureFetcher : public ICoopTask
{
public:
    TextureFetcher(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        std::vector<std::string> textureUris);
    ~TextureFetcher() override;
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    /// Begins the task.
    Result<> Begin() override;

    /// Updates the task.  This must be called periodically while IsPending() returns true.
    /// In addition this task depends on the FileFetcher to be updated periodically, so the
    /// caller must ensure that the FileFetcher is updated as well.
    void Update() override;

    /// Returns true if the task is running (started but not complete).
    bool IsPending() const override;

    /// Returns the collection of textures if the task succeeded, otherwise returns an error.
    /// This method will invalidate the task, so it can only be called once.
    Result<std::vector<wgpu::Texture>> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Succeeded,
        Failed,
    };

    class FetchTask : public ICoopTask
    {
    public:

        FetchTask(const GpuHelper& gpuHelper,
            FileFetcher& fileFetcher,
            ThreadPool& threadPool,
            std::string uri,
            wgpu::CommandEncoder commandEncoder);

        FetchTask() = delete;
        ~FetchTask() override;
        FetchTask(const FetchTask&) = delete;
        FetchTask& operator=(const FetchTask&) = delete;
        FetchTask(FetchTask&&) = delete;
        FetchTask& operator=(FetchTask&&) = delete;

        Result<> Begin() override;

        void Update() override;

        bool IsPending() const override;

        Result<wgpu::Texture> Take();

    private:
        enum class Stage
        {
            None,
            Fetching,
            Decoding,
            Succeeded,
            Failed
        };

        Result<> BeginDecode();

        Result<> Decode() const;

        // Worker thread entry point for decoding the texture.
        static void Decode(void* userData);

        Result<> CommitStagingBuffer();

        friend TextureFetcher;

        const GpuHelper* m_GpuHelper{ nullptr };
        FileFetcher* m_FileFetcher{ nullptr };
        ThreadPool* m_ThreadPool{ nullptr };
        std::string m_Uri;
        FetchRequestId m_FetchRequestId{};
        std::vector<uint8_t> m_FetchedData;
        wgpu::Texture m_Texture{ nullptr };
        wgpu::Buffer m_StagingBuffer{ nullptr };
        wgpu::CommandEncoder m_CommandEncoder{ nullptr };
        std::byte* m_MappedMemory{ nullptr };
        Result<> m_DecodeResult;

        std::atomic<bool> m_CompletionFlag{ false };

        Stage m_Stage{ Stage::None };
    };

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    std::deque<FetchTask> m_Tasks;
    std::optional<CoopTaskBatch> m_TaskBatch;
    std::vector<wgpu::Texture> m_Textures;
    std::vector<std::string> m_TextureUris;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};