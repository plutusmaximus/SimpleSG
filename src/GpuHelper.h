#pragma once

#include "Result.h"
#include "shaders/GpuBufferTypes.h"

#include <string_view>

#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

class GpuHelper final
{
public:
    GpuHelper() = delete;
    ~GpuHelper() = delete;
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = delete;
    GpuHelper& operator=(GpuHelper&&) = delete;

    static Result<> Startup(const char* appName);

    static void Shutdown();

    static SDL_Window* GetWindow();
    static wgpu::Instance GetInstance();
    static wgpu::Device GetDevice();
    static wgpu::Surface GetSurface();
    static wgpu::Texture GetDefaultTexture();
    static wgpu::Sampler GetDefaultSampler();
    static Extent GetScreenBounds();    
    static Result<wgpu::Texture> GetSwapChainTexture();
    static wgpu::TextureFormat GetSwapChainFormat();

    static Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Creates an empty texture with the given dimensions and name.
    static Result<wgpu::Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string_view& name);

    /// @brief Creates a staging buffer for copying texture data to the GPU.
    static Result<wgpu::Buffer> CreateStagingBuffer(wgpu::Texture texture,
        const std::string_view& name);

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    static Result<> CommitStagingBuffer(wgpu::Texture texture, wgpu::Buffer stagingBuffer);

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    static Result<> CommitStagingBuffer(
        wgpu::Texture texture, wgpu::Buffer stagingBuffer, wgpu::CommandEncoder cmdEncoder);

    static Result<VertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view& name);

    static Result<IndexBuffer> CreateIndexBuffer(const size_t count, const std::string_view& name);

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    static Result<T> CreateStorageBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_storage_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Storage");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateStorageBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    static Result<T> CreateUniformBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_uniform_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Uniform");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateUniformBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed indirect buffer.
    template<typename T>
    static Result<T> CreateIndirectBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_indirect_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Indirect");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    template<typename T>
    static size_t AlignUniformBuffer()
    {
        wgpu::Limits limits;
        GetDevice().GetLimits(&limits);
        const size_t alignment = limits.minUniformBufferOffsetAlignment;

        return (sizeof(T) + alignment - 1) & ~(alignment - 1);
    }

private:

    static Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size, const std::string_view& name);
    static Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string_view& name);
    static Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string_view& name);
};