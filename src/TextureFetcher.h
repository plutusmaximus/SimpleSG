#pragma once

#include "Result.h"

#include <filesystem>
#include <memory>
#include <vector>

class GpuHelper;
class ThreadPool;
class FileFetcher;

namespace wgpu
{
class Texture;
class CommandEncoder;
} // namespace wgpu

class TextureFetcher
{
public:
    TextureFetcher(const GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        std::filesystem::path basePath,
        std::vector<std::string> textureUris);

    TextureFetcher() = delete;
    ~TextureFetcher();
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    /// @brief Begins the task.
    Result<> Begin();

    /// @brief Updates the task.  This must be called periodically until IsComplete() returns
    /// true.
    void Update();

    /// @brief Returns true if the task is running (started but not complete).
    bool IsRunning() const;

    /// @brief Returns true if the task is complete (either succeeded or failed).
    bool IsComplete() const;

    /// @brief Returns true if the task succeeded.
    bool Succeeded() const;

    /// @brief Returns the collection of textures if the task succeeded, otherwise returns an error.
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
    wgpu::CommandEncoder* m_CmdEncoder{ nullptr };

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};