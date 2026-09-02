#pragma once

#include "Result.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

class GpuHelper;
class ThreadPool;
class FileFetcher;

namespace detail
{
class TextureLoadTask;
}

namespace wgpu
{
class Texture;
}

class TextureFetcher
{
public:
    TextureFetcher(const GpuHelper& gpuHelper,
        ThreadPool& threadPool,
        FileFetcher& fileFetcher,
        std::filesystem::path basePath,
        const std::span<const std::string_view>& textureUris);

    TextureFetcher() = delete;
    ~TextureFetcher();
    TextureFetcher(const TextureFetcher&) = delete;
    TextureFetcher& operator=(const TextureFetcher&) = delete;
    TextureFetcher(TextureFetcher&&) = delete;
    TextureFetcher& operator=(TextureFetcher&&) = delete;

    void Update();

    bool IsComplete() const { return m_Stage == Stage::Succeeded || m_Stage == Stage::Failed; }

    bool Succeeded() const { return m_Stage == Stage::Succeeded; }

    std::span<const wgpu::Texture> GetTextures() const;

private:
    enum class Stage
    {
        Begin,
        Fetching,
        Succeeded,
        Failed,
    };

    Result<> Begin();

    const GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    std::filesystem::path m_BasePath;
    std::vector<std::string> m_TextureUris;
    std::vector<std::unique_ptr<detail::TextureLoadTask>> m_TaskHeap;
    std::vector<detail::TextureLoadTask*> m_Tasks;
    std::vector<wgpu::Texture> m_Textures;

    Stage m_Stage{ Stage::Begin };
};