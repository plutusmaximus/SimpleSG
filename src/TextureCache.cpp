#include "TextureCache.h"

Result<>
TextureCache::Startup()
{
    MLG_CHECKV(WebgpuHelper::GetDevice(), "TextureCache::Initialize called before WebgpuHelper::Startup");
    MLG_CHECKV(!m_Initialized, "TextureCache::Initialize called more than once");

    constexpr uint32_t kDefaultTextureWidth = 128;
    constexpr uint32_t kDefaultTextureHeight = 128;

    auto defaultTexture = WebgpuHelper::CreateTexture(
        kDefaultTextureWidth,
        kDefaultTextureHeight,
        "DefaultTexture");

    MLG_CHECK(defaultTexture);

    auto mapped = defaultTexture->MapBytes();
    MLG_CHECK(mapped);

    std::byte* data = mapped->data();

    for(uint32_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        for(uint32_t x = 0; x < kDefaultTextureWidth; ++x, data += 4)
        {
            //Magenta
            data[0] = std::byte{0xFF};
            data[1] = std::byte{0x00};
            data[2] = std::byte{0xFF};
            data[3] = std::byte{0xFF};
        }
    }

    defaultTexture->Unmap();

    const wgpu::SamplerDescriptor samplerDesc//
    {
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .addressModeW = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Linear,
    };

    m_DefaultTexture = *defaultTexture;
    m_DefaultSampler = WebgpuHelper::GetDevice().CreateSampler(&samplerDesc);

    return Result<>::Ok;
}

Result<>
TextureCache::Shutdown()
{
    Clear();
    m_DefaultTexture = nullptr;
    m_DefaultSampler = nullptr;
    m_Initialized = false;
    return Result<>::Ok;
}
