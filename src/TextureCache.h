#pragma once

#include "StringArena.h"

#include <webgpu/webgpu_cpp.h>

class GpuHelper;

class TextureCache
{
public:
    explicit TextureCache(GpuHelper& gpuHelper)
    : m_GpuHelper(&gpuHelper)
    {
    }
    ~TextureCache() = default;
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&) = delete;
    TextureCache& operator=(TextureCache&&) = delete;

    bool Contains(const std::string_view& uri) const;

    void AddOrReplace(const std::string_view& uri, wgpu::Texture texture);

    wgpu::Texture Get(const std::string_view& uri) const;

    void Clear();

private:

    GpuHelper* m_GpuHelper;

    static constexpr size_t kStringArenaChunkSize = 1024uz * 10uz;

    StringArena m_StringArena{kStringArenaChunkSize};

    std::unordered_map<std::string_view, wgpu::Texture> m_Textures;
};