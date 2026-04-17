#include "TextureCache.h"

Result<>
TextureCache::Initialize()
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

    auto mapped = defaultTexture->Map();
    MLG_CHECK(mapped);

    uint8_t* data = static_cast<uint8_t*>(*mapped);

    for(uint32_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        for(uint32_t x = 0; x < kDefaultTextureWidth; ++x, data += 4)
        {
            //Magenta
            data[0] = 0xFF;
            data[1] = 0x00;
            data[2] = 0xFF;
            data[3] = 0xFF;
        }
    }

    defaultTexture->Unmap();

    wgpu::SamplerDescriptor samplerDesc//
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