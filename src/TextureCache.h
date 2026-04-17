#pragma once

#include "WebgpuHelper.h"

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


    bool Contains(const std::string& uri) const
    {
        return m_Textures.contains(uri);
    }

    void AddOrReplace(const std::string& uri, Texture&& texture)
    {
        m_Textures[uri] = std::move(texture);
    }

    void AddOrReplace(const std::string& uri, const Texture& texture)
    {
        m_Textures[uri] = texture;
    }

    Texture& Get(const std::string& uri)
    {
        return m_Textures[uri];
    }

    const Texture& Get(const std::string& uri) const
    {
        return m_Textures.at(uri);
    }

    void Clear()
    {
        m_Textures.clear();
    }

    Texture& GetDefaultTexture() { return m_DefaultTexture; }
    const Texture& GetDefaultTexture() const { return m_DefaultTexture; }
    wgpu::Sampler& GetDefaultSampler() { return m_DefaultSampler; }
    const wgpu::Sampler& GetDefaultSampler() const { return m_DefaultSampler; }

private:

    std::unordered_map<std::string, Texture> m_Textures;

    wgpu::Sampler m_DefaultSampler;
    Texture m_DefaultTexture;

    bool m_Initialized{ false };
};