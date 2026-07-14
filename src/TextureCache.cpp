#include "TextureCache.h"

#include "Log.h"

bool
TextureCache::Contains(const std::string_view& uri) const
{
    return m_Textures.contains(uri);
}

void
TextureCache::AddOrReplace(const std::string_view& uri, wgpu::Texture texture)
{
    const StringHandle uriHandle = m_StringArena.NewString(uri);
    m_Textures.emplace(uriHandle, std::move(texture));
}

wgpu::Texture
TextureCache::Get(const std::string_view& uri) const
{
    auto it = m_Textures.find(uri);
    if(m_Textures.end() == it)
    {
        MLG_ERROR("TextureCache does not contain a texture with URI: {}", uri);
        return m_DefaultTexture;
    }
    return it->second;
}

void
TextureCache::Clear()
{
    m_Textures.clear();
}
