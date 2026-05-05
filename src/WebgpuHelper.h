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

    Result<std::span<std::byte>> MapBytes();

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

class BasicGpuBuffer : private wgpu::Buffer
{
public:
    using wgpu::Buffer::Buffer;
    using wgpu::Buffer::GetSize;
    using wgpu::Buffer::operator bool;

    wgpu::Buffer& GetGpuBuffer() { return *this; }
    const wgpu::Buffer& GetGpuBuffer() const { return *this; }

    Result<std::span<std::byte>> MapBytes();

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

template<typename T>
class MappedGpuBuffer
{
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(!std::is_pointer_v<T>);
    static_assert(!std::is_reference_v<T>);

public:

    using value_type = T;

    explicit MappedGpuBuffer(std::span<std::byte> bytes)
        : m_Bytes(bytes)
    {
        MLG_ASSERT(bytes.size_bytes() % sizeof(T) == 0);
    }

    std::size_t size() const { return m_Bytes.size_bytes() / sizeof(T); }

    T Load(std::size_t index) const
    {
        MLG_ASSERT(index < size());

        T value;
        std::memcpy(&value, m_Bytes.data() + index * sizeof(T), sizeof(T));

        return value;
    }

    void Store(std::size_t index, const T& value)
    {
        MLG_ASSERT(index < size());

        std::memcpy(m_Bytes.data() + index * sizeof(T), &value, sizeof(T));
    }

    std::span<std::byte> Bytes() { return m_Bytes; }

    std::span<const std::byte> Bytes() const { return m_Bytes; }

private:
    std::span<std::byte> m_Bytes;
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

    Result<MappedGpuBuffer<T>> Map()
    {
        auto bytes = BasicGpuBuffer::MapBytes();
        MLG_CHECK(bytes);

        return MappedGpuBuffer<T>{ *bytes };
    }

private:
    friend class WebgpuHelper;

    explicit SemanticGpuBuffer(wgpu::Buffer buffer)
        : BasicGpuBuffer(buffer)
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

    static Result<VertexBuffer> CreateVertexBuffer(const size_t size, const std::string& name);

    static Result<IndexBuffer> CreateIndexBuffer(const size_t size, const std::string& name);

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateSemanticStorageBuffer(const size_t size, const std::string& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");
        static_assert(!std::is_same_v<T, DrawIndirectBuffer>, "Use CreateIndirectBuffer to create indirect buffers");

        auto bufferResult = CreateStorageBuffer(size, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateSemanticUniformBuffer(const size_t size, const std::string& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");
        static_assert(!std::is_same_v<T, DrawIndirectBuffer>, "Use CreateIndirectBuffer to create indirect buffers");

        auto bufferResult = CreateUniformBuffer(size, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    /// @brief Creates a semantically-typed indirect buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateSemanticIndirectBuffer(const size_t size, const std::string& name)
    {
        static_assert(!std::is_same_v<T, VertexBuffer>, "Use CreateVertexBuffer to create vertex buffers");
        static_assert(!std::is_same_v<T, IndexBuffer>, "Use CreateIndexBuffer to create index buffers");

        auto bufferResult = CreateIndirectBuffer(size, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
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

    static Result<wgpu::Buffer> CreateIndirectBuffer(const size_t size, const std::string& name);
    static Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string& name);
    static Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string& name);
};