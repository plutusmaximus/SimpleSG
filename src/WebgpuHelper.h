#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;
class imstring;

class WebgpuTextureStagingBuffer final
{
public:

    WebgpuTextureStagingBuffer(wgpu::Buffer buffer, const uint32_t rowStride)
        : m_Buffer(buffer), m_RowStride(rowStride)
    {
    };

    WebgpuTextureStagingBuffer() = delete;

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

    uint32_t GetRowStride() const { return m_RowStride; }

private:
    wgpu::Buffer m_Buffer;
    uint32_t m_RowStride;
};

class WebgpuHelper final
{
public:

    static Result<> Startup(const char* appName);

    static void Shutdown();

    static SDL_Window* GetWindow();
    static wgpu::Instance GetInstance();
    static wgpu::Device GetDevice();
    static wgpu::Surface GetSurface();

    /// @brief Creates an empty texture with the given dimensions and name.
    static Result<wgpu::Texture> CreateTexture(
        const unsigned width, const unsigned height, const imstring& name);

    /// @brief Creates a texture filled with the given color.
    static Result<wgpu::Texture> CreateTexture(const RgbaColorf& color, const imstring& name);

    /// @brief Creates a texture filled with the given pixel data. The pixel data should be in RGBA8 format.
    static Result<wgpu::Texture> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name);

    /// @brief Creates a staging buffer for the given texture. The staging buffer can be used to
    /// upload texture data to the GPU.
    static Result<WebgpuTextureStagingBuffer> CreateTextureStagingBuffer(wgpu::Texture texture);

    /// @brief Uploads texture data from a staging buffer to a texture.
    static Result<> UploadTextureData(wgpu::Texture texture,
        WebgpuTextureStagingBuffer stagingBuffer,
        wgpu::CommandEncoder encoder);

    static Result<wgpu::Sampler> GetDefaultSampler();

    static Result<wgpu::Buffer> CreateVertexBuffer(const size_t size, const imstring& name);

    static Result<wgpu::Buffer> CreateIndexBuffer(const size_t size, const imstring& name);
};