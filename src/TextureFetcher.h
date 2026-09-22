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

/// Loads textures. A failed load uses the default texture for that slot.
/// Keep calling FileFetcher::ProcessCompletions() while this task is running.
/// Calling Update() alone does not process file completions.
class TextureFetcher : public ICoopTask<>
{
public:
    TextureFetcher(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        std::vector<std::string> texturePaths);
    ~TextureFetcher() override = default;
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

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

    Result<> OnStart() override;

    void OnUpdate() override;

    class FetchTask : public ICoopTask<>
    {
    public:

        FetchTask(const GpuHelper& gpuHelper,
            FileFetcher& fileFetcher,
            ThreadPool& threadPool,
            std::string path,
            wgpu::CommandEncoder commandEncoder);

        FetchTask() = delete;
        ~FetchTask() override = default;
        FetchTask(const FetchTask&) = delete;
        FetchTask& operator=(const FetchTask&) = delete;
        FetchTask(FetchTask&&) = delete;
        FetchTask& operator=(FetchTask&&) = delete;

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

        Result<> OnStart() override;

        void OnUpdate() override;

        Result<> BeginDecode();

        Result<> Decode();

        // Worker thread entry point for decoding the texture.
        static void Decode(void* userData);

        Result<> CommitStagingBuffer();

        friend TextureFetcher;

        const GpuHelper* m_GpuHelper{ nullptr };
        FileFetcher* m_FileFetcher{ nullptr };
        ThreadPool* m_ThreadPool{ nullptr };
        std::string m_Path;
        FetchRequestId m_FetchRequestId{};
        std::vector<uint8_t> m_FetchedData;
        wgpu::Texture m_Texture{ nullptr };
        wgpu::Buffer m_StagingBuffer{ nullptr };
        wgpu::CommandEncoder m_CommandEncoder{ nullptr };
        std::span<std::byte> m_MappedMemory;
        Result<> m_DecodeResult;

        // We cannot access webgpu objects directly in the worker thread,
        // so we cache the necessary properties here.
        uint32_t m_TexWidth{ 0 };
        uint32_t m_TexHeight{ 0 };
        wgpu::TextureFormat m_TexFormat{ wgpu::TextureFormat::Undefined };

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
    std::vector<std::string> m_TexturePaths;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};