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
        // Passed to the adapter request callback to store the result of the request.
        struct AdapterRequestData
        {
            Result<WGPUAdapter> Result;
            std::atomic<bool> IsComplete{ false };
        };

        // Passed to the device request callback to store the result of the request.
        struct DeviceRequestData
        {
            Result<WGPUDevice> Result;
            std::atomic<bool> IsComplete{ false };
        };

        explicit CreateTask(std::string appName);
        ~CreateTask();
        CreateTask(const CreateTask&) = delete;
        CreateTask& operator=(const CreateTask&) = delete;
        CreateTask(CreateTask&&) = delete;
        CreateTask& operator=(CreateTask&&) = delete;

        /// @brief Begins the task.
        Result<> Begin();

        /// @brief Updates the task.  This must be called periodically until IsComplete() returns
        /// true.
        void Update();

        /// @brief Returns true if the task is running (started but not complete).
        bool IsRunning() const;

        /// @brief Returns true if the task is complete (either succeeded or failed).
        bool IsComplete() const;

        /// @brief Returns true if the task succeeded.
        bool Succeeded() const;

        /// @brief Returns the GpuHelper instance if the task succeeded, otherwise returns an error.
        /// @note This method will invalidate the task, so it can only be called once.
        Result<std::unique_ptr<GpuHelper>> Take();

    private:
        friend GpuHelper;

        enum class Stage
        {
            None,
            CreateAdapter,
            CreatingAdapter,
            CreatingDevice,
            Succeeded,
            Failed
        };

        Result<> CreateAdapter();
        Result<> FinalizeAdapter();
        Result<> CreateDevice();
        Result<> FinalizeDevice();
        Result<> Configure();

        std::string m_AppName;

        AdapterRequestData m_AdapterRequestData;
        DeviceRequestData m_DeviceRequestData;

        std::unique_ptr<GpuHelper> m_GpuHelper;

        Stage m_Stage{ Stage::None };

        bool m_Consumed{ false };
    };

    ~GpuHelper();
    GpuHelper(const GpuHelper&) = delete;
    GpuHelper& operator=(const GpuHelper&) = delete;
    GpuHelper(GpuHelper&&) = delete;
    GpuHelper& operator=(GpuHelper&&) = delete;

    SDL_Window* GetWindow() const;
    const wgpu::Instance& GetInstance() const;
    const wgpu::Device& GetDevice() const;
    const wgpu::Surface& GetSurface() const;
    const wgpu::Texture& GetDefaultTexture() const;
    const wgpu::Sampler& GetDefaultSampler() const;
    Dimension2 GetScreenDimensions() const;
    Result<GpuRenderTarget> GetSwapChainTexture() const;
    wgpu::TextureFormat GetSwapChainFormat() const;

    /// @brief Resizes the swap chain to the given width and height.
    Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Loads a shader from the given file path.
    /// FIXME(KB) - need an async version of this.
    Result<wgpu::ShaderModule> LoadShader(const std::string_view& filePath,
        FileFetcher& fileFetcher) const;

    /// @brief Creates an empty texture with the given dimensions and name.
    Result<wgpu::Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string_view& name) const;

    /// @brief Creates a render target with the given dimensions and name.
    Result<GpuRenderTarget> CreateRenderTarget(
        const unsigned width, const unsigned height, const std::string_view& name) const;

    /// @brief Creates a depth buffer with the given dimensions and name.
    Result<GpuDepthTarget> CreateDepthBuffer(
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
    Result<GpuVertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view& name) const;

    /// @brief Creates an index buffer with capacity for the given number of indices.
    Result<GpuIndexBuffer> CreateIndexBuffer(const size_t count,
        const std::string_view& name) const;

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

    SDL_Window* m_Window{ nullptr };
    SDL_MetalView m_MetalView{ nullptr };
    wgpu::Instance m_Instance{ nullptr };
    wgpu::Adapter m_Adapter{ nullptr };
    wgpu::Device m_Device{ nullptr };
    wgpu::Surface m_Surface{ nullptr };
    mutable wgpu::TextureFormat m_SurfaceFormat{ wgpu::TextureFormat::Undefined };
    wgpu::Texture m_DefaultTexture{ nullptr };
    wgpu::Sampler m_DefaultSampler{ nullptr };
};