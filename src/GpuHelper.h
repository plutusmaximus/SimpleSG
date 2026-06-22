#pragma once

#include "Result.h"
#include "shaders/ShaderInterop.h"
#include "VecMath.h"
#include "Vertex.h"

#include <span>
#include <string_view>
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

class Texture
{
public:

    Texture() = delete;
    ~Texture() = default;
    Texture(const Texture&) = default;
    Texture& operator=(const Texture&) = default;
    Texture(Texture&&) = default;
    Texture& operator=(Texture&&) = default;

    uint32_t GetWidth() const { return GetGpuTexture().GetWidth(); }
    uint32_t GetHeight() const { return GetGpuTexture().GetHeight(); }
    wgpu::TextureFormat GetFormat() const { return GetGpuTexture().GetFormat(); }
    explicit operator bool() const { return static_cast<bool>(m_GpuTexture); }

    const wgpu::Texture& GetGpuTexture() const { return m_GpuTexture; }

    // Row stride is the number of bytes between the start of one row of pixels and the start of the
    // next row. For optimal performance, rows must be aligned to 256 bytes, so we round up to the
    // nearest multiple of 256.
    size_t GetRowStride() const;

    Result<std::span<std::byte>> MapBytes();

    Result<> Unmap();

    Result<> Unmap(const wgpu::CommandEncoder& cmdEncoder);

private:

    friend class GpuHelper;

    explicit Texture(wgpu::Texture texture);

    wgpu::Texture m_GpuTexture;
    wgpu::Buffer m_StagingBuffer;
};

class BasicGpuBuffer
{
public:

    BasicGpuBuffer() = delete;
    ~BasicGpuBuffer() = default;
    BasicGpuBuffer(const BasicGpuBuffer&) = default;
    BasicGpuBuffer& operator=(const BasicGpuBuffer&) = default;
    BasicGpuBuffer(BasicGpuBuffer&&) = default;
    BasicGpuBuffer& operator=(BasicGpuBuffer&&) = default;

    explicit operator bool() const { return static_cast<bool>(m_GpuBuffer); }

    const wgpu::Buffer& GetGpuBuffer() const { return m_GpuBuffer; }

    size_t BufferSize() const { return m_GpuBuffer.GetSize(); }

protected:

    explicit BasicGpuBuffer(wgpu::Buffer buffer);

private:

    wgpu::Buffer m_GpuBuffer;
};

template<typename T>
class SemanticGpuBuffer : public BasicGpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(!std::is_pointer_v<T>);
    static_assert(!std::is_reference_v<T>);

public:
    using value_type = T;
    using BasicGpuBuffer::operator bool;

    SemanticGpuBuffer() = delete;
    ~SemanticGpuBuffer() = default;
    SemanticGpuBuffer(const SemanticGpuBuffer&) = default;
    SemanticGpuBuffer& operator=(const SemanticGpuBuffer&) = default;
    SemanticGpuBuffer(SemanticGpuBuffer&&) = default;
    SemanticGpuBuffer& operator=(SemanticGpuBuffer&&) = default;

    size_t Count() const { return BufferSize() / sizeof(T); }

    // Stores a single value at the given index.
    void Store(std::size_t index, const T& value)
    {
        Store(index, std::span<const T>(&value, 1));
    }

    // Stores an array of values starting at the given index.
    void Store(std::size_t index, std::span<const T> values);

    // Stores an array of values starting at the zero index.
    void Store(std::span<const T> values) { Store(0, values); }

private:
    friend class GpuHelper;

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

    static Result<> Resize(const uint32_t width, const uint32_t height);

    /// @brief Creates an empty texture with the given dimensions and name.
    static Result<Texture> CreateTexture(
        const unsigned width, const unsigned height, const std::string_view& name);

    static Result<VertexBuffer> CreateVertexBuffer(const size_t count,
        const std::string_view& name);

    static Result<IndexBuffer> CreateIndexBuffer(const size_t count, const std::string_view& name);

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
SemanticGpuBuffer<T>::Store(std::size_t index, std::span<const T> values)
{
    const size_t offset = index * sizeof(T);

    MLG_ASSERT((offset + (values.size() * sizeof(T))) <= BufferSize(), "Index out of bounds");

    GpuHelper::GetDevice().GetQueue().WriteBuffer(GetGpuBuffer(),
        offset,
        values.data(),
        values.size() * sizeof(T));
}