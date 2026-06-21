#pragma once

#include "GpuHelper.h"
#include "StringArena.h"

class TextureCache
{
public:
    TextureCache() = default;
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&) = delete;
    TextureCache& operator=(TextureCache&&) = delete;

    Result<> Startup();

    Result<> Shutdown();

    ~TextureCache()
    {
        Shutdown();
    }

    bool Contains(const std::string_view& uri) const
    {
        return m_Textures.contains(uri);
    }

    void AddOrReplace(const std::string_view& uri, Texture&& texture)
    {
        const StringHandle uriHandle = m_StringArena.NewString(uri);
        m_Textures.emplace(uriHandle, std::move(texture));
    }

    const Texture& Get(const std::string_view& uri) const
    {
        auto it = m_Textures.find(uri);
        if(m_Textures.end() == it)
        {
            MLG_ERROR("TextureCache does not contain a texture with URI: {}", uri);
            return GetDefaultTexture();
        }
        return it->second;
    }

    void Clear()
    {
        m_Textures.clear();
    }

    const Texture& GetDefaultTexture() const { return *m_DefaultTexture; }
    const wgpu::Sampler& GetDefaultSampler() const { return m_DefaultSampler; }

private:

    static constexpr size_t kStringArenaChunkSize = 1024uz * 10uz;

    StringArena m_StringArena{kStringArenaChunkSize};

    std::unordered_map<std::string_view, Texture> m_Textures;

    wgpu::Sampler m_DefaultSampler;
    Result<Texture> m_DefaultTexture;

    bool m_Initialized{ false };
};