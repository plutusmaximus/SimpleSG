#pragma once

#include "Result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class System;

class TextureFetcher;
using FetchRequestId = uint64_t;

namespace wgpu
{
class Texture;
}

class TextureFetcher
{
public:
    TextureFetcher(
        System& system, std::filesystem::path basePath, std::vector<std::string> textureUris);

    TextureFetcher() = delete;
    ~TextureFetcher();
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    /// Begins the task.
    Result<> Begin();

    /// Updates the task.  This must be called periodically while IsPending() returns true.
    void Update();

    /// Returns true if the task is running (started but not complete).
    bool IsPending() const;

    /// Returns the collection of textures if the task succeeded, otherwise returns an error.
    /// @note This method will invalidate the task, so it can only be called once.
    Result<std::vector<wgpu::Texture>> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Succeeded,
        Failed,
    };

    class FetchTask
    {
    public:
        enum class Stage
        {
            None,
            Fetching,
            Decoding,
            Succeeded,
            Failed
        };

        FetchTask(const std::filesystem::path& basePath,
            std::string baseUri,
            System& system,
            wgpu::CommandEncoder commandEncoder);

        FetchTask() = delete;
        ~FetchTask();
        FetchTask(const FetchTask&) = delete;
        FetchTask& operator=(const FetchTask&) = delete;
        FetchTask(FetchTask&&) = delete;
        FetchTask& operator=(FetchTask&&) = delete;

        Result<> Begin();

        void Update();

        bool IsPending() const;

        Result<wgpu::Texture> Take();

    private:
        Result<> BeginDecode();

        Result<> Decode() const;

        // Worker thread entry point for decoding the texture.
        static void Decode(void* userData);

        Result<> CommitStagingBuffer();

        friend TextureFetcher;

        std::string m_Uri;
        std::string m_FullPath;
        System* m_System{ nullptr };
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

    struct PendingTask
    {
        FetchTask* Task;
        size_t Index;
    };

    System* m_System{ nullptr };
    std::filesystem::path m_BasePath;
    std::vector<std::string> m_TextureUris;
    std::deque<FetchTask> m_TaskStorage;
    std::vector<PendingTask> m_PendingTasks;
    std::vector<wgpu::Texture> m_Textures;
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};