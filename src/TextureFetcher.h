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
}

class TextureFetcher
{
public:
    static Result<TextureFetcher> Create(const GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        std::filesystem::path basePath,
        std::vector<std::string> textureUris);

    TextureFetcher() = delete;
    ~TextureFetcher();
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) noexcept;
    TextureFetcher& operator=(TextureFetcher&&) noexcept;

    /// @brief Updates the task.  This must be called periodically until IsComplete() returns
    /// true.
    void Update();

    /// @brief Returns true if the task is complete (either succeeded or failed).
    bool IsComplete() const;

    /// @brief Returns true if the task succeeded.
    bool Succeeded() const;

    /// @brief Returns the collection of textures if the task succeeded, otherwise returns an error.
    /// @note This method will invalidate the task, so it can only be called once.

    Result<std::vector<wgpu::Texture>> Take();

    /// @brief Returns true if the task is valid and can be updated.
    /// Returns false if the task has been invalidated by calling Take().
    bool IsValid() const;

private:
    class Impl;

    explicit TextureFetcher(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_Impl;
};