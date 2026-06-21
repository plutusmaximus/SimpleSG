#include "TextureCache.h"

Result<>
TextureCache::Startup()
{
    MLG_CHECKV(GpuHelper::GetDevice(), "TextureCache::Startup called before GpuHelper::Startup");
    MLG_CHECKV(!m_Initialized, "TextureCache::Startup called more than once");

    constexpr size_t kDefaultTextureWidth = 128;
    constexpr size_t kDefaultTextureHeight = 128;
    constexpr RgbaColoru8 kDefaultTextureColor{ "#FF00FFFF"_rgba }; // Magenta

    auto defaultTexture = GpuHelper::CreateTexture(
        kDefaultTextureWidth,
        kDefaultTextureHeight,
        "DefaultTexture");

    MLG_CHECK(defaultTexture);

    auto mapped = defaultTexture->MapBytes();
    MLG_CHECK(mapped);

    void* data = mapped->data();
    const std::span<uint8_t> mappedSpan(static_cast<uint8_t*>(data), mapped->size());

    const size_t rowStride = defaultTexture->GetRowStride();

    for(size_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        size_t offset = y * rowStride;

        for(size_t x = 0; x < kDefaultTextureWidth; ++x, offset += 4)
        {
            //Magenta
            mappedSpan[offset + 0] = kDefaultTextureColor.r;
            mappedSpan[offset + 1] = kDefaultTextureColor.g;
            mappedSpan[offset + 2] = kDefaultTextureColor.b;
            mappedSpan[offset + 3] = kDefaultTextureColor.a;
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

    m_DefaultTexture = std::move(*defaultTexture);
    m_DefaultSampler = GpuHelper::GetDevice().CreateSampler(&samplerDesc);

    return Result<>::Ok;
}

Result<>
TextureCache::Shutdown()
{
    Clear();
    m_StringArena = StringArena{kStringArenaChunkSize};
    m_DefaultTexture = {};
    m_DefaultSampler = {};
    m_Initialized = false;
    return Result<>::Ok;
}
