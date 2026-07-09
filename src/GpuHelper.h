#pragma once

#include "Result.h"
#include "VecMath.h"
#include "shaders/GpuBufferTypes.h"

#include <string_view>

struct SDL_Window;

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

namespace mlg::detail
{
class GpuHelperImpl;
} // namespace mlg::detail

class GpuHelper final
{
public:
    ~GpuHelper() = default;
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = default;
    GpuHelper& operator=(GpuHelper&&) = default;

    SDL_Window* GetWindow() const;
    wgpu::Instance GetInstance() const;
    wgpu::Device GetDevice() const;
    wgpu::Surface GetSurface() const;
    wgpu::Texture GetDefaultTexture() const;
    wgpu::Sampler GetDefaultSampler() const;
    Dimension2 GetScreenDimensions() const;    
    Result<wgpu::Texture> GetSwapChainTexture() const;
    wgpu::TextureFormat GetSwapChainFormat() const;

    static Result<GpuHelper> Create(const char* appName);

    Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Creates an empty texture with the given dimensions and name.
    Result<wgpu::Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string_view& name);

    /// @brief Creates a staging buffer for copying texture data to the GPU.
    Result<wgpu::Buffer> CreateStagingBuffer(wgpu::Texture texture,
        const std::string_view& name);

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    Result<> CommitStagingBuffer(wgpu::Texture texture, wgpu::Buffer stagingBuffer);

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    static Result<> CommitStagingBuffer(
        wgpu::Texture texture, wgpu::Buffer stagingBuffer, wgpu::CommandEncoder cmdEncoder);

    Result<VertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view& name);

    Result<IndexBuffer> CreateIndexBuffer(const size_t count, const std::string_view& name);

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    Result<T> CreateStorageBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_storage_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Storage");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateStorageBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    Result<T> CreateUniformBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_uniform_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Uniform");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateUniformBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed indirect buffer.
    template<typename T>
    Result<T> CreateIndirectBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(is_gpu_indirect_buffer_type_v<T>, "T must be a SemanticGpuBuffer type with BufferType::Indirect");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(GetDevice(), *bufferResult);
    }

    template<typename T>
    size_t AlignUniformBuffer()
    {
        wgpu::Limits limits;
        GetDevice().GetLimits(&limits);
        const size_t alignment = limits.minUniformBufferOffsetAlignment;

        return (sizeof(T) + alignment - 1) & ~(alignment - 1);
    }

private:

    static void Deleter(mlg::detail::GpuHelperImpl*);

    using DeleterType = decltype(&Deleter);
    using UniquePtrType = std::unique_ptr<mlg::detail::GpuHelperImpl, DeleterType>;

    explicit GpuHelper(UniquePtrType impl) : m_Impl(std::move(impl)) {}

    enum class BufferMappedState
    {
        Unmapped,
        Mapped,
    };

    Result<wgpu::Buffer> CreateGpuBuffer(const wgpu::BufferUsage usage,
        const size_t size,
        BufferMappedState mappedState,
        const std::string_view name);
        
    Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size, const std::string_view& name);
    Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string_view& name);
    Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string_view& name);

    UniquePtrType m_Impl{ nullptr, &GpuHelper::Deleter };
};