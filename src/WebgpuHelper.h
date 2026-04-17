#pragma once

#include "Result.h"
#include "VecMath.h"

#include <array>
#include <string>

#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

class BasicGpuBuffer : private wgpu::Buffer
{
public:

    using wgpu::Buffer::Buffer;
    using wgpu::Buffer::GetSize;
    using wgpu::Buffer::operator bool;

    wgpu::Buffer& GetGpuBuffer() { return *this; }
    const wgpu::Buffer& GetGpuBuffer() const { return *this; }

    Result<void*> Map();

    Result<> Unmap();

    Result<> Unmap(wgpu::CommandEncoder cmdEncoder);

protected:
    explicit BasicGpuBuffer(wgpu::Buffer buffer)
        : wgpu::Buffer(buffer)
    {
    }

private:

    wgpu::Buffer m_StagingBuffer;
};

class Texture : private wgpu::Texture
{
public:

    using wgpu::Texture::Texture;
    using wgpu::Texture::GetWidth;
    using wgpu::Texture::GetHeight;
    using wgpu::Texture::GetFormat;
    using wgpu::Texture::GetUsage;
    using wgpu::Texture::CreateView;
    using wgpu::Texture::operator bool;

    wgpu::Texture& GetGpuTexture() { return *this; }
    const wgpu::Texture& GetGpuTexture() const { return *this; }

    Result<void*> Map();

    Result<> Unmap();

    Result<> Unmap(wgpu::CommandEncoder cmdEncoder);

protected:
    friend class WebgpuHelper;

    explicit Texture(wgpu::Texture texture)
        : wgpu::Texture(texture)
    {
    }

private:

    wgpu::Buffer m_StagingBuffer;
};

template<typename T>
class TypedGpuBuffer : public BasicGpuBuffer
{
public:

    using BasicGpuBuffer::BasicGpuBuffer;
    using BasicGpuBuffer::operator bool;

private:
    friend class WebgpuHelper;

    explicit TypedGpuBuffer(wgpu::Buffer buffer)
        : BasicGpuBuffer(buffer)
    {
    }
};

struct VertexBufferTag{};
struct IndexBufferTag{};

using VertexBuffer = TypedGpuBuffer<VertexBufferTag>;
using IndexBuffer = TypedGpuBuffer<IndexBufferTag>;

class WebgpuHelper final
{
public:

    static Result<> Startup(const char* appName);

    static void Shutdown();

    static SDL_Window* GetWindow();
    static wgpu::Instance GetInstance();
    static wgpu::Device GetDevice();
    static wgpu::Surface GetSurface();

    static Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Creates an empty texture with the given dimensions and name.
    static Result<Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string& name);

    static Result<wgpu::Sampler> GetDefaultSampler();

    static Result<VertexBuffer> CreateVertexBuffer(const size_t size, const std::string& name);

    static Result<IndexBuffer> CreateIndexBuffer(const size_t size, const std::string& name);

    static Result<const std::array<wgpu::BindGroupLayout, 3>> GetColorPipelineLayouts();

    static Result<const std::array<wgpu::BindGroupLayout, 3>> GetTransformPipelineLayouts();

    static Result<const std::array<wgpu::BindGroupLayout, 3>> GetCompositorPipelineLayouts();

    static Extent GetScreenBounds();

    static wgpu::TextureFormat GetSwapChainFormat();

    template<typename T>
    static size_t AlignUniformBuffer()
    {
        wgpu::Limits limits;
        GetDevice().GetLimits(&limits);
        const size_t alignment = limits.minUniformBufferOffsetAlignment;

        return (sizeof(T) + alignment - 1) & ~(alignment - 1);
    }
};