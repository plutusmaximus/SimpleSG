#pragma once

#include "Result.h"
#include "shaders/ShaderInterop.h"
#include "VecMath.h"
#include "Vertex.h"

#include <array>
#include <span>
#include <string>
#include <type_traits>

#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

// A note on using staging buffers to upload data to the GPU:
// When copying data to textures we use a staging buffer.
// When copying data to other buffers we could also use a staging buffer, but it's simpler to use
// Queue::WriteBuffer which doesn't require a staging buffer. The equivalent of WriteBuffer for
// textures is Queue::WriteTexture howerver Queue::WriteTexture contains a bunch of validation and
// is slow compared to using a staging buffer and CopyBufferToTexture.

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

    // Row stride is the number of bytes between the start of one row of pixels and the start of the
    // next row. For optimal performance, rows must be aligned to 256 bytes, so we round up to the
    // nearest multiple of 256.
    size_t GetRowStride() const;

    Result<std::span<std::byte>> MapBytes();

    Result<> Unmap();

    Result<> Unmap(const wgpu::CommandEncoder& cmdEncoder);

protected:
    friend class WebgpuHelper;

    explicit Texture(wgpu::Texture texture)
        : wgpu::Texture(std::move(texture))
    {
    }

private:

    wgpu::Buffer m_StagingBuffer;
};

class BasicGpuBuffer : private wgpu::Buffer
{
public:
    using wgpu::Buffer::Buffer;
    using wgpu::Buffer::operator bool;

    wgpu::Buffer& GetGpuBuffer() { return *this; }
    const wgpu::Buffer& GetGpuBuffer() const { return *this; }

    size_t BufferSize() const { return GetSize(); }

    explicit BasicGpuBuffer(wgpu::Buffer buffer)
        : wgpu::Buffer(std::move(buffer))
    {
    }
};

template<typename T>
class SemanticGpuBuffer : public BasicGpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(!std::is_pointer_v<T>);
    static_assert(!std::is_reference_v<T>);

public:
    using value_type = T;
    using BasicGpuBuffer::BasicGpuBuffer;
    using BasicGpuBuffer::operator bool;

    size_t Count() const { return BufferSize() / sizeof(T); }

    // Stores a single value at the given index.
    void Store(std::size_t index, const T& value);

    // Stores an array of values starting at the given index.
    void Store(std::size_t index, std::span<const T> values);

private:
    friend class WebgpuHelper;

    explicit SemanticGpuBuffer(wgpu::Buffer buffer)
        : BasicGpuBuffer(std::move(buffer))
    {
    }
};

using VertexBuffer = SemanticGpuBuffer<Vertex>;
using IndexBuffer = SemanticGpuBuffer<VertexIndex>;
using DrawIndirectBuffer = SemanticGpuBuffer<ShaderInterop::DrawIndirectParams>;

template <typename T>
struct is_gpu_buffer_type : std::false_type {};
template <typename Tag>
struct is_gpu_buffer_type<SemanticGpuBuffer<Tag>> : std::true_type {};
template <typename T>
inline constexpr bool is_gpu_buffer_type_v = is_gpu_buffer_type<T>::value;

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

    static Result<VertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view& name);

    static Result<VertexBuffer> CreateVertexBuffer(std::span<const Vertex> vertices,
        const std::string_view& name);

    static Result<IndexBuffer> CreateIndexBuffer(const size_t count, const std::string_view& name);

    static Result<IndexBuffer> CreateIndexBuffer(std::span<const VertexIndex> indices,
        const std::string_view& name);

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateStorageBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");
        static_assert(!std::is_same_v<T, DrawIndirectBuffer>, "Use CreateIndirectBuffer to create indirect buffers");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateStorageBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    template<typename T>
        requires is_gpu_buffer_type_v<T>
    static Result<T> CreateStorageBuffer(std::span<const typename T::value_type> values,
        const std::string_view& name)
    {
        auto buffer = CreateStorageBuffer<T>(values.size() * sizeof(typename T::value_type), name);
        MLG_CHECK(buffer);

        buffer->Store(0, values);
        return *buffer;
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateUniformBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");
        static_assert(!std::is_same_v<T, DrawIndirectBuffer>, "Use CreateIndirectBuffer to create indirect buffers");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateUniformBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    template<typename T>
        requires is_gpu_buffer_type_v<T>
    static Result<T> CreateUniformBuffer(std::span<const typename T::value_type> values,
        const std::string_view& name)
    {
        auto buffer = CreateUniformBuffer<T>(values.size() * sizeof(typename T::value_type), name);
        MLG_CHECK(buffer);

        buffer->Store(0, values);
        return *buffer;
    }

    /// @brief Creates a semantically-typed indirect buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateIndirectBuffer(const size_t count, const std::string_view& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");

        const size_t bufferSize = count * sizeof(typename T::value_type);
        auto bufferResult = CreateIndirectBuffer(bufferSize, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    template<typename T>
        requires is_gpu_buffer_type_v<T>
    static Result<T> CreateIndirectBuffer(std::span<const typename T::value_type> values,
        const std::string_view& name)
    {
        auto buffer = CreateIndirectBuffer<T>(values.size() * sizeof(typename T::value_type), name);
        MLG_CHECK(buffer);

        buffer->Store(0, values);
        return *buffer;
    }

    static Result<const std::array<wgpu::BindGroupLayout, 2>> GetColorPipelineLayouts();

    static Result<const std::array<wgpu::BindGroupLayout, 1>> GetTransformPipelineLayouts();

    static Result<const std::array<wgpu::BindGroupLayout, 1>> GetCompositorPipelineLayouts();

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

private:

    static Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size, const std::string_view& name);
    static Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string_view& name);
    static Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string_view& name);
};

template<typename T>
inline void
SemanticGpuBuffer<T>::Store(std::size_t index, const T& value)
{
    const size_t offset = index * sizeof(T);

    MLG_ASSERT(offset < BufferSize(), "Index out of bounds");

    WebgpuHelper::GetDevice().GetQueue().WriteBuffer(GetGpuBuffer(), offset, &value, sizeof(T));
}

template<typename T>
inline void
SemanticGpuBuffer<T>::Store(std::size_t index, std::span<const T> values)
{
    const size_t offset = index * sizeof(T);

    MLG_ASSERT((offset + values.size() * sizeof(T)) <= BufferSize(), "Index out of bounds");

    WebgpuHelper::GetDevice().GetQueue().WriteBuffer(GetGpuBuffer(),
        offset,
        values.data(),
        values.size() * sizeof(T));
}