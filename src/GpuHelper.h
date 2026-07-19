#pragma once

#include "GpuTypes.h"
#include "VecMath.h"

#include <atomic>
#include <memory>
#include <string_view>

class FileFetcher;
struct SDL_Window;
using SDL_MetalView = void*;

class GpuHelper final
{
public:
    static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
    static constexpr size_t kNumTextureChannels = 4;
    static constexpr wgpu::TextureFormat kDepthBufferFormat = wgpu::TextureFormat::Depth24Plus;

    /// @brief A task that creates a GpuHelper instance asynchronously.
    class CreateTask
    {
    public:
        enum class State
        {
            None,
            CreateAdapter,
            CreatingAdapter,
            CreatingDevice,
            Succeeded,
            Failed
        };

        // Passed to the adapter request callback to store the result of the request.
        struct AdapterRequestData
        {
            Result<wgpu::Adapter> Result;
            std::atomic<bool> IsComplete{ false };
        };

        // Passed to the device request callback to store the result of the request.
        struct DeviceRequestData
        {
            Result<wgpu::Device> Result;
            std::atomic<bool> IsComplete{ false };
        };

        ~CreateTask() = default;
        CreateTask(const CreateTask&) = delete;
        CreateTask& operator=(const CreateTask&) = delete;
        CreateTask(CreateTask&&) = default;
        CreateTask& operator=(CreateTask&&) = default;

        /// @brief Updates the task.  This must be called periodically until IsComplete() returns
        /// true.
        Result<> Update();

        /// @brief Returns true if the task is valid and can be updated.
        /// Returns false if the task has been invalidated by calling Get().
        bool IsValid() const;

        /// @brief Returns true if the task is complete (either succeeded or failed).
        bool IsComplete() const;

        /// @brief Returns true if the task succeeded.
        bool Succeeded() const;

        /// @brief Returns the GpuHelper instance if the task succeeded, otherwise returns an error.
        /// @note This method will invalidate the task, so it can only be called once.
        Result<std::unique_ptr<GpuHelper>> Get();

    private:
        friend GpuHelper;

        // Only callable by GpuHelper
        CreateTask() = default;

        Result<> Begin(const std::string_view& appName);
        Result<> CreateAdapter();
        Result<> FinalizeAdapter();
        Result<> CreateDevice();
        Result<> FinalizeDevice();

        void Invalidate();

        // To make the task moveable we keep it's implementation state
        // in a separate Impl struct that is heap-allocated and managed by a unique_ptr.
        struct Impl
        {
            Impl() = default;
            ~Impl() = default;
            Impl(const Impl&) = delete;
            Impl& operator=(const Impl&) = delete;
            Impl(Impl&&) = delete;
            Impl& operator=(Impl&&) = delete;

            GpuHelper::CreateTask::AdapterRequestData m_AdapterRequestData;
            GpuHelper::CreateTask::DeviceRequestData m_DeviceRequestData;

            std::unique_ptr<GpuHelper> m_GpuHelper;

            CreateTask::State m_State{ CreateTask::State::None };
        };

        std::unique_ptr<Impl> m_TaskImpl;
    };

    ~GpuHelper();
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = delete;
    GpuHelper& operator=(GpuHelper&&) = delete;

    /// @brief Creates a GpuHelper instance asynchronously.
    ///
    static Result<CreateTask> Create(const std::string_view& appName);

    SDL_Window* GetWindow() const;
    const wgpu::Instance& GetInstance() const;
    const wgpu::Device& GetDevice() const;
    const wgpu::Surface& GetSurface() const;
    const wgpu::Texture& GetDefaultTexture() const;
    const wgpu::Sampler& GetDefaultSampler() const;
    const wgpu::BindGroupLayout& GetTextureBindGroupLayout() const;
    Dimension2 GetScreenDimensions() const;
    Result<ValidTexture> GetSwapChainTexture() const;
    wgpu::TextureFormat GetSwapChainFormat() const;

    /// @brief Resizes the swap chain to the given width and height.
    Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Loads a shader from the given file path.
    /// FIXME(KB) - need an async version of this.
    Result<ValidShaderModule> LoadShader(const std::string_view& filePath,
        FileFetcher& fileFetcher) const;

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
            "T must be a GpuBuffer type with GpuBufferUsage::Storage");

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
            "T must be a GpuBuffer type with GpuBufferUsage::Uniform");

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
            "T must be a GpuBuffer type with GpuBufferUsage::Indirect");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T::Create(GetDevice(), *bufferResult);
    }

    /// @brief Returns the aligned row stride for a texture staging buffer.
    /// Texture staging buffer rows must be a multiple of 256 bytes.
    /// @param textureWidth The width of the texture in pixels.
    /// @return The aligned row stride in bytes.
    static size_t GetTextureAlignedRowStride(const size_t textureWidth);

private:
    GpuHelper() = default;

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

    SDL_Window* Window{ nullptr };
    SDL_MetalView MetalView{ nullptr };
    wgpu::Instance Instance{ nullptr };
    wgpu::Adapter Adapter{ nullptr };
    wgpu::Device Device{ nullptr };
    wgpu::Surface Surface{ nullptr };
    mutable wgpu::TextureFormat SurfaceFormat{ wgpu::TextureFormat::Undefined };
    wgpu::BindGroupLayout TextureBindGroupLayout{ nullptr };
    wgpu::Texture DefaultTexture{ nullptr };
    wgpu::Sampler DefaultSampler{ nullptr };
};