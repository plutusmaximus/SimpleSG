#pragma once

#include "GpuTypes.h"
#include "Result.h"
#include "VecMath.h"

#include <memory.h>
#include <string_view>

class FileFetcher;
struct SDL_Window;

template<typename T>
class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

class GpuHelper final
{    
    class Impl;
    class FutureImpl;

public:
    static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
    static constexpr wgpu::TextureFormat kDepthBufferFormat = wgpu::TextureFormat::Depth24Plus;

    class Future
    {
    public:
        Future() = delete;
        ~Future() = default;
        Future(const Future&) = delete;
        Future& operator=(const Future&) = delete;
        Future(Future&&) = default;
        Future& operator=(Future&&) = default;

        bool IsValid() const;

        Result<> Update();

        bool IsComplete() const;

        bool Succeeded() const;

        /// @brief Returns the GpuHelper if the future succeeded, otherwise returns an error.
        /// @note This method will invalidate the future, so it can only be called once.
        Result<GpuHelper> Get();

    private:
        friend GpuHelper;

        static void Deleter(FutureImpl*);

        using DeleterType = decltype(&Deleter);
        using UniquePtrType = std::unique_ptr<FutureImpl, DeleterType>;

        explicit Future(UniquePtrType impl)
            : m_Impl(std::move(impl))
        {
        }

        UniquePtrType m_Impl{ nullptr, &Deleter };
    };

    ~GpuHelper() = default;
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = default;
    GpuHelper& operator=(GpuHelper&&) = default;

    static Result<Future> Create(const char* appName);

    SDL_Window* GetWindow() const;
    wgpu::Instance GetInstance() const;
    wgpu::Device GetDevice() const;
    wgpu::Surface GetSurface() const;
    wgpu::Texture GetDefaultTexture() const;
    wgpu::Sampler GetDefaultSampler() const;
    wgpu::BindGroupLayout GetTextureBindGroupLayout() const;
    Dimension2 GetScreenDimensions() const;
    Result<ValidTexture> GetSwapChainTexture() const;
    wgpu::TextureFormat GetSwapChainFormat() const;

    /// @brief Resizes the swap chain to the given width and height.
    Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Loads a shader from the given file path.
    /// FIXME(KB) - need an async version of this.
    Result<ValidShaderModule> LoadShader(const char* filePath, FileFetcher& fileFetcher) const;

    /// @brief Creates an empty texture with the given dimensions and name.
    Result<wgpu::Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string_view& name) const;

    /// @brief Creates a bind group that includes the texture and the default sampler.
    Result<wgpu::BindGroup> CreateTextureBindGroup(const wgpu::Texture& texture,
        const std::string_view& name) const;

    /// @brief Creates a render target with the given dimensions and name.
    Result<ValidTexture> CreateRenderTarget(
        const unsigned width, const unsigned height, const std::string_view& name) const;

    /// @brief Creates a depth buffer with the given dimensions and name.
    Result<ValidTexture> CreateDepthBuffer(
        const unsigned width, const unsigned height, const std::string_view& name) const;

    /// @brief Creates a staging buffer for copying texture data to the GPU.
    Result<wgpu::Buffer> CreateStagingBuffer(wgpu::Texture texture,
        const std::string_view& name) const;

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    Result<> CommitStagingBuffer(wgpu::Texture texture, wgpu::Buffer stagingBuffer) const;

    /// @brief Commits the data in the staging buffer to texture memory on the GPU.
    static Result<> CommitStagingBuffer(
        wgpu::Texture texture, wgpu::Buffer stagingBuffer, wgpu::CommandEncoder cmdEncoder);

    /// @brief Creates a vertex buffer with capacity for the given number of vertices.
    Result<VertexBuffer> CreateVertexBuffer(const size_t count, const std::string_view& name) const;

    /// @brief Creates an index buffer with capacity for the given number of indices.
    Result<IndexBuffer> CreateIndexBuffer(const size_t count, const std::string_view& name) const;

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    Result<T> CreateStorageBuffer(const size_t count, const std::string_view& name) const
    {
        static_assert(is_gpu_storage_buffer_type_v<T>,
            "T must be a SemanticGpuBuffer type with BufferType::Storage");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateStorageBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    Result<T> CreateUniformBuffer(const size_t count, const std::string_view& name) const
    {
        static_assert(is_gpu_uniform_buffer_type_v<T>,
            "T must be a SemanticGpuBuffer type with BufferType::Uniform");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateUniformBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

    /// @brief Creates a semantically-typed indirect buffer.
    template<typename T>
    Result<T> CreateIndirectBuffer(const size_t count, const std::string_view& name) const
    {
        static_assert(is_gpu_indirect_buffer_type_v<T>,
            "T must be a SemanticGpuBuffer type with BufferType::Indirect");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

private:

    static void Deleter(Impl*);

    using DeleterType = decltype(&Deleter);
    using UniquePtrType = std::unique_ptr<Impl, DeleterType>;

    explicit GpuHelper(UniquePtrType impl)
        : m_Impl(std::move(impl))
    {
    }

    enum class BufferMappedState
    {
        Unmapped,
        Mapped,
    };

    Result<wgpu::Buffer> CreateGpuBuffer(const wgpu::BufferUsage usage,
        const size_t size,
        BufferMappedState mappedState,
        const std::string_view name) const;

    Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size,
        const std::string_view& name) const;
    Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string_view& name) const;
    Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string_view& name) const;

    UniquePtrType m_Impl{ nullptr, &Deleter };
};