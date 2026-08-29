#pragma once

#include "Result.h"

#include <filesystem>
#include <span>
#include <vector>

class GpuHelper;
class ThreadPool;
class FileFetcher;
class TextureCache;
struct MaterialDef;

namespace detail
{
class TextureLoadTask;
}

class TextureFetcher
{
public:
    TextureFetcher(GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        TextureCache& textureCache,
        std::filesystem::path basePath,
        const std::span<const MaterialDef>& materialDefs);

    TextureFetcher() = delete;
    ~TextureFetcher();
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    void Update();

    bool IsComplete() const { return m_State == State::Succeeded || m_State == State::Failed; }

    bool Succeeded() const { return m_State == State::Succeeded; }

private:
    enum class State
    {
        Begin,
        Fetching,
        Succeeded,
        Failed,
    };

    Result<> Begin();

    GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    TextureCache* m_TextureCache{ nullptr };
    std::filesystem::path m_BasePath;
    std::span<const MaterialDef> m_MaterialDefs;
    std::vector<detail::TextureLoadTask> m_TaskHeap;
    std::vector<detail::TextureLoadTask*> m_Tasks;
    std::vector<std::atomic<bool>> m_CompletionFlags;

    State m_State{ State::Begin };
};