#pragma once

#include "Result.h"
#include "shaders/ShaderTypes.h"
#include "VecMath.h"
#include "Vertex.h"

#include <array>
#include <string>
#include <type_traits>

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
    static_assert(!std::is_reference_v<T>, "TypedGpuBuffer cannot be instantiated with reference types");
    static_assert(!std::is_pointer_v<T>, "TypedGpuBuffer cannot be instantiated with pointer types");
public:

    using BasicGpuBuffer::BasicGpuBuffer;
    using BasicGpuBuffer::operator bool;

    Result<T*> Map()
    {
        auto mapping = BasicGpuBuffer::Map();
        MLG_CHECK(mapping);
        return static_cast<T*>(*mapping);
    }

private:
    friend class WebgpuHelper;

    explicit TypedGpuBuffer(wgpu::Buffer buffer)
        : BasicGpuBuffer(buffer)
    {
    }
};

using VertexBuffer = TypedGpuBuffer<Vertex>;
using IndexBuffer = TypedGpuBuffer<VertexIndex>;
using IndirectBuffer = TypedGpuBuffer<ShaderTypes::DrawIndirectParams>;

template <typename T>
struct is_gpu_buffer_type : std::false_type {};
template <typename Tag>
struct is_gpu_buffer_type<TypedGpuBuffer<Tag>> : std::true_type {};
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

    static Result<IndirectBuffer> CreateIndirectBuffer(const size_t size, const std::string& name);

    /// @brief Creates a semantically-typed storage buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateTypedStorageBuffer(const size_t size, const std::string& name)
    {
        // Don't try to create "special" buffers with this helper.
        // Use the specific helper functions for vertex/index/uniform/indirect buffers.
        static_assert(!std::is_same_v<T, VertexBuffer>);
        static_assert(!std::is_same_v<T, IndexBuffer>);
        static_assert(!std::is_same_v<T, IndirectBuffer>);

        auto bufferResult = CreateStorageBuffer(size, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    /// @brief Creates a semantically-typed uniform buffer.
    template<typename T>
    requires is_gpu_buffer_type_v<T>
    static Result<T> CreateTypedUniformBuffer(const size_t size, const std::string& name)
    {
        // Don't try to create "special" buffers with this helper.
        // Use the specific helper functions for vertex/index/uniform/indirect buffers.
        static_assert(!std::is_same_v<T, VertexBuffer>);
        static_assert(!std::is_same_v<T, IndexBuffer>);
        static_assert(!std::is_same_v<T, IndirectBuffer>);

        auto bufferResult = CreateUniformBuffer(size, name);
        MLG_CHECK(bufferResult);

        return T(*bufferResult);
    }

    static Result<const std::array<wgpu::BindGroupLayout, 3>> GetColorPipelineLayouts();

    static Result<const std::array<wgpu::BindGroupLayout, 2>> GetTransformPipelineLayouts();

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

private:

    static Result<wgpu::Buffer> CreateStorageBuffer(const size_t size, const std::string& name);
    static Result<wgpu::Buffer> CreateUniformBuffer(const size_t size, const std::string& name);
};